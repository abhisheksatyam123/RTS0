# RTS0 plan (0 A.D. native port)

> **Status 2026-09-06:** `simulation.cpp:7394` + 11 ticks, roster 23 units / 14 bldgs / 13 civs, `grid_nav.h:722` A*+hierarchical+string-pull, manifest 663 models / 1316 anims / 98 skels / 652 terrain, `check_parity.py` 92% overall (Wave 1+2 refresh 2026-10-06) (G6 models 11.8% is the long tail). `src/0ad/` stays reference-only (SConstruct excludes it).

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

DONE 2026-10-05: `save_replay`/`load_replay` round-trip verified by `tools/test_replay.cpp` (357 lines, real TurnManager API, negative control, RC=0); headless two-client harness `tools/playtest_lockstep.py` PASS 3/3 seeds × 2000 turns `--shadow --through` (see `docs/MULTIPLAYER.md` "Playtest harness"). DONE 2026-10-06: first live OOS FOUND + FIXED (pre-Start lockstep ticked divergent boot worlds -> `net_game_started_` gate, main_node.h). **LANDED (Wave 1+2, grep-verified):** start gate flag `src/presentation/main_node.h:185` `bool net_game_started_ = false;`; host broadcast of the authoritative setup `src/presentation/main_node.cpp:3743-3745` (`turn_mgr_.mark_game_started(sim)`, `net_host_.note_game_start_sent()`, `net_host_.sendGameStart(rtso::format_game_start_full(setup))`); joiner consumption of `NMT_GAME_START` `src/presentation/main_node.cpp:3803-3810` (`apply_net_game_start` → `take_pending_game_start_full` → `turn_mgr_.note_game_start_applied(sim)`). Two-client automation verified EXIT 0 with strict start markers — see docs/MULTIPLAYER.md. Still needed: 5-minute (vs 1-minute) session run; long-drift OOS hunt (Petra `rand()`, wall-drag spawn).

DONE 2026-10-08: two-process UDP session. The in-process harness above gave both sims the same batch, so it could not see network timing. `tools/test_netcode.cpp --net-peer` runs host and joiner as separate processes with their own jittered clocks over real UDP with injected loss. Before the fix it went OOS on turn 1; it now runs 1500 turns (5 minutes real time, 5% loss) with identical hashes. Fixes: lockstep gate + `COMMAND_DELAY` 2 (`turn_manager.cpp`, cites `NetServerTurnManager.cpp:43`), remote batches no longer applied twice (onCommand + onMessage), reliable UDP no longer loses packets on loss (per-packet ACK, per-peer seq, gap-aware dedupe in `net_host.cpp`), joiner keeps batches that arrive with GAME_START. Runs in CI via `playtest_lockstep.py` (600 turns, x8 speed, 5% loss). Still open: joiner commands team 0, not its own slot (`apply_game_setup` selects team-0 units; the sim has no per-client ownership check); >2 players need host relay of client batches.

Verify: `python3 tools/lobby_server.py` then two `./build/linux/RTS0.x86_64` Host + Join.

### P2 — Long-path JPS + dirty chunks (remaining)

Cardinal jump + per-chunk dirty rebuild DONE 2026-09-06 (`grid_nav.h` `flood_single_chunk`/`rebuild_dirty_chunks` + diff-based dirty in `rebuild_nav_grid`; single building = 1 chunk flood, 128k-query equivalence 0 mismatches). Remaining: optional `PASS_CAVALRY` slope class.

### P3 — Petra naval remaining

`moveApart` + fisher counts are in. DONE 2026-10-05: per-sea fleet registry (`petra_ai.h` sea_warships[8][16], sea id via `global_region_at(...,PASS_SHIP)`, cites navalManager.js:58/:546) + island `PLAN_RAID` dock targets (`find_petra_attack_target` same-sea dock P100/P80, coastal barracks P60/P40, cites attackPlan.js:930/:1100-1108, entityExtend.js:134-158; `check_parity --unit petra` 28/28). **LANDED 2026-10-06 (Wave 1+2, grep-verified) — `maintainFleet` upkeep is DONE:** generation `src/network/turn_manager.cpp:544` emits `NET_CMD_MAINTAIN_FLEET` (`net_types.h:134` = 24) once per turn (built-dock gate + own-queue-empty + wishlist-vs-fleet, `turn_manager.cpp:481-551`, citing navalManager.js:507-543); dispatch `turn_manager.cpp:559 maintain_fleet_upkeep(Simulation&)` called from `tick()` at `turn_manager.cpp:861`; **sim hook landed** `src/simulation/simulation.h:195 bool cmd_maintain_fleet(uint32_t building_id, UnitType u_type, uint8_t team)` implemented at `src/simulation/simulation.cpp:835` (team-aware naval queue seam). Remaining (non-blocking): per-sea wishlist registry on the Simulation side — `generate_fleet_upkeep_commands` currently uses `active_seas = 1` (`turn_manager.cpp:524`), so trireme/fisher targets scale correctly but not per-sea.

### P4 — Session UI split + civ actors

**0 A.D.:** `binaries/data/mods/public/gui/session/` (TopPanel, selection, production, diplomacy, minimap, chat)  
**RTS0:** monolithic coordinator `src/presentation/main_node.cpp:3961`

1. Split HUD widgets only if P1 playtest is clean (not before). **Gate MET headless 2026-10-05**: `playtest_lockstep.py` 3 seeds x 2000 turns PASS 0 OOS (incl. `--shadow --through`), replay round-trip PASS — real 2-Godot-client session still the final gate.
2. Per-civ GLB/actor variants (`art/actors/`) after shared `model_id`s — do not add more cavalry fallbacks.

#### P4 Session HUD Modularization Plan (Split `main_node.cpp:3961`)

`RtsoMain` in `src/presentation/main_node.cpp` currently coordinates root scene lifecycle, 3D camera controls, selection raycasts, audio playback, net lockstep synchronization, and the entire 2D UI presentation tree (~3,960 lines). The P4 UI modularization separates the monolithic presentation layer into discrete Godot `Control` widgets mirroring 0 A.D.'s `gui/session/` architecture.

##### 1. Target Widget Architecture & File List

| Widget File | 0 A.D. Source Counterpart | Scope & Extracted Lines from `main_node.cpp` | Key Dependencies / APIs |
|---|---|---|---|
| `src/presentation/chat_window.h / .cpp` | `gui/session/chat/ChatWindow.js`, `ChatBounce.js`, `gui/session/chat/chat.xml` | Chat overlay (`_ready:890-970`), channel filter buttons (All/Allies/Team/Enemies `chat_addressee_bar`), send & parse (`toggle_chat:3869`, `_on_chat_send_pressed:3884`, `_on_chat_text_submitted:3917`). | `NetHost` chat broadcast (`NMT_CHAT`), local history `RichTextLabel`. Emits `chat_sent(channel, text)`. |
| `src/presentation/diplomacy_dialog.h / .cpp` | `gui/session/diplomacy/DiplomacyDialog.js`, `TributeDialog.js`, `diplomacy.xml` | Stance matrix & tribute UI (`_ready:972-1018`), stance button dispatch (`toggle_diplomacy:3921`, `_on_diplo_stance_changed:3929`), tribute transfer (`_on_diplo_tribute_pressed:3946`). | Lockstep cmd 22 (`post_lockstep(22)` stance) & resource deduction. Emits `stance_changed`, `tribute_sent`. |
| `src/presentation/lobby_dialog.h / .cpp` | `gui/gamesetup/`, `NetClient.js`, `gamesetup.xml` | Multiplayer room browser, host/join forms (`_ready:1020-1120`), room refresh (`refresh_lobby_rooms:3601`), host/start/join lifecycle (`_on_lobby_host_pressed:3638`, `_on_lobby_start_pressed:3660`, `_on_lobby_join_pressed:3788`). | `NetHost`, `LobbyClient` HTTP JSON client (`lobby_http_post_json`), `GameSetupParams`. |
| `src/presentation/minimap_view.h / .cpp` (decoupling completion) | `gui/session/minimap/MiniMapPanel.js`, `MiniMap.xml` | FoW 3-state shroud (`ShroudMode`), diplomacy palette coloring, camera frustum quadrilateral, ping & tactical flare (`_on_flare_requested:1460`, `_on_minimap_clicked:1466`, flare signal :249). | `Simulation::fow_grid`, `Camera3D` unproject, emits `flare_requested(Vector2)`, `world_point_clicked(Vector2)`. |
| `src/presentation/inspector_view.h / .cpp` | `gui/session/panel_entity/PanelEntity.js`, `selection_details/`, `PanelEntities.xml` | Entity details card (`_ready:339-440`), unit/building portrait, class title, rank badge, 5-bar stat stack (HP/Capture/XP/Stamina/Aura `make_insp_bar`), state update (`update_inspector:2041-2296`). | Read-only queries to `Simulation::units()` and `Simulation::buildings()`. |
| `src/presentation/command_queue_view.h / .cpp` | `gui/session/selection_panels.js`, `production_queue.xml`, `unit_training.xml`, `formation_panel.xml` | 12 formation buttons (`_ready:442-570`), 5 stances (`_ready:630-643`), 8-slot queue grid (`_ready:2318-2335`), build menu triggers, slot input & cancel (`_on_queue_slot_gui_input:3844`, `_on_queue_slot_cancel:3854`, `update_command_panel:2297-2523`). | Lockstep cmds 6 (train), 7 (stance), 8 (formation), 20 (cancel train). Emits order signals to `RtsoMain`. |
| `src/presentation/main_node.h / .cpp` (slim coordinator) | `gui/session/session.js` | Top HUD counters (resources, pop, wave clock), camera rig manipulation, raycast selection & box drag, order routing to `Simulation` / `TurnManager`, audio cues. (Reduces from 3,961 to ~900 lines). | Holds widget instances as child `Control` nodes, connects signals. |

##### 2. Staged Extraction Order & Migration Protocol

To preserve determinism and avoid regression in live multiplayer playtests:

1. **Phase 4.1: Chat & Diplomacy Leaf Extractions**
   - Extract `chat_window.h/.cpp` and `diplomacy_dialog.h/.cpp`.
   - Rationale: Leaf modal overlays with zero incoming dependencies and small state surfaces. All interaction flows via signals or direct calls to `NetHost` / `TurnManager`.
   - Verification: `test_diplo_net` and syntax checks pass.

2. **Phase 4.2: Lobby Dialog Extraction**
   - Extract `lobby_dialog.h/.cpp`.
   - Rationale: Active only pre-match or during game setup. Communicates with `RtsoMain` exclusively through `GameSetupParams` and net host state.
   - Verification: Headless 2-peer connection handshake test passes.

3. **Phase 4.3: MiniMap Interface Polish**
   - Decouple remaining minimap event hooks (`flare_requested`, `shroud_cycle`, `minimap_clicked`) into clean signals in `minimap_view.h/.cpp`.
   - Verification: Minimap flare and FoW 3-state rendering pass visual audit.

4. **Phase 4.4: Inspector Read-Only Widget Extraction**
   - Extract `inspector_view.h/.cpp`.
   - Rationale: Read-only observer receiving selection IDs from `RtsoMain` and querying `Simulation` views (`units()`, `buildings()`, `resources()`). Never posts lockstep commands directly.
   - Verification: Unit stat bars, aura icons, and experience ranks render correctly.

5. **Phase 4.5: Command & Production Queue Extraction**
   - Extract `command_queue_view.h/.cpp`.
   - Rationale: Contains the highest interaction density (formations, stances, training queues, build modal hotkeys). Exposes command request signals (`train_requested`, `stance_requested`, `formation_requested`, `queue_canceled`) connected to `RtsoMain::post_lockstep`.
   - Verification: `playtest_lockstep.py` (3 seeds x 2000 turns) stays at 0 OOS.

6. **Phase 4.6: RtsoMain Final Coordinator Slimming**
   - `RtsoMain` retains high-level orchestration, Godot scene hooks (`_ready`, `_process`, `_unhandled_input`), 3D spatial picking, and bridge between UI signals and network lockstep. Target size: ~900-1000 lines.

### P5 — Ranked lockstep (optional, last)

**0 A.D.:** `maths/Fixed.h` `CFixed_15_16`  
Only if 1v1 ranked is a goal. Until then keep float sim + host-authoritative hash checks.

---

## Working rules

- Read the 0 A.D. function first; port semantics, not JS.
- New Petra/nav/net code cites `file:line` in a one-line comment.
- Do not compile `src/0ad/`.
- `docs/REMAINING_GAPS.md` body below the G1–G10 table may still describe old stubs — trust the table + this file.
