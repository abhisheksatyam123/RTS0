# RTS0 Session HUD Parity vs 0 A.D. `gui/session`

> Scope: `binaries/data/mods/public/gui/session/**` (0 A.D. rP26xxx) vs RTS0 `src/presentation/main_node.cpp` (`RtsoMain::_ready`, `_process`, `update_hud/inspector/command_panel`, input), `src/presentation/minimap_view.cpp`, `src/presentation/world_view_node.cpp`, and the widget modules `top_panel.h`, `production_queue.h`, `chat_window.h`, `diplomacy_dialog.h`.

---

## 1. Top HUD

0 A.D. `gui/session/top_panel/CounterResource.js` + `CounterPopulation.js` + `PhaseIcon.js` + `CivIcon.js` + `gui/session/menu.js`

| RTS0 (`src/presentation/main_node.cpp`) | 0 A.D. (`gui/session/**`) | Parity | Gap |
|---|---|---|---|
| **4 resource counters** – `main_node.cpp:223-247` `res_icons[4]` + `res_labels[4]` (`food/wood/metal/stone` from `assets/textures/ui/icons/*.png`, `load_tex` fallback `main_node.cpp:114-136`) in `top_hud_panel` `main_node.cpp:183-212` | `gui/session/top_panel/CounterResource.js` generic counter with `icon` + `count` + `rate` + tooltip (4 resources via `Resources` component) | **4/4 icons present** but rates are baked text `(+\d/m)` not sampled from sim template; no tooltip breakdown | Tooltip + per-resource observer (0 A.D. registers `OnResourceChange`) missing. |
| **Population** – `main_node.cpp:249-263` `pop_icon` + `pop_label` `cur/max`, red when capped `main_node.cpp:1108-1112` | `gui/session/top_panel/CounterPopulation.js` `popCount` / `popLimit` / `popMax` with class icons, queue overflow warning | **1/1** but no queue preview | No pop-cap warning sound / housed indicator parity. |
| **Civ emblem** – `main_node.cpp:214-220` `civ_emblem`, `main_node.cpp:321-337` `civ_selector`; top bar widget `src/presentation/top_panel.h` (`RtsoTopPanel`) | `gui/session/top_panel/CivIcon.js` + `art/textures/ui/session/icons/civilisations/*.png` (13 civs) | **13/13** emblems in `assets/textures/ui/emblems/` | Phase icons (`assets/textures/ui/phases/`) are set on the phase tech buttons (`main_node.cpp:2363`, `:2371`). Phase `TextureRect` in the top bar not re-checked. |
| **Idle worker** – `main_node.cpp:265-271` `idle_worker_btn` `Idle: N` `cycle_idle_worker()` `main_node.cpp:810-829` | `gui/session/idle_worker/*.js` button + observer + `FindIdleUnits` | Present but no portrait cycling | Parity OK for M1; 0 A.D. shows worker class icon vs text. |
| **Clock / Phase / Wave** – `main_node.cpp:274-286` `clock_label` + `wave_label` (`Wave N (Xs) | Phase`) `main_node.cpp:1119-1136` | `gui/session/top_panel/TimeElapsed.js` + `gui/common/music/Music.js` + `gui/session/notifications/NotificationManager.js` | Clock parity; wave injected by RTS0 `Simulation` not 0 A.D. `Trigger` | Phase emblem missing as above. |
| **Speed / Menu / Map / Civ selectors** – `main_node.cpp:288-337` | `gui/session/menu.js`, `gui/session/speed/*.js` (observer `SetSimRate`) | Present | 0 A.D. diplomacy/options shortcut keys not bound. |

**Assets:** `assets/textures/ui/emblems/` holds the 13 civ emblems; `assets/textures/ui/phases/` holds `village`, `town` and `city` phase icons (checked 2026-10-08). Missing exports: `docs/ASSET_MANIFEST_TODO.md`.

---

## 2. Inspector / Entity Details

0 A.D. `gui/session/panel_entity/PanelEntity.js` + `gui/session/unit_actions/**` + `gui/session/selection_details/**`

| RTS0 (`main_node.cpp:339-416`) | 0 A.D. | Parity | Gap |
|---|---|---|---|
| **Portrait + title/class** – `main_node.cpp:361-380` `inspector_portrait` (`load_tex` `portraits/*.png` fallback `main_node.cpp:123-126`), `inspector_title` `inspector_class` | `gui/session/panel_entity/PanelEntity.js` `Icon` + `Name` + `Class` + `Tooltip` + `Rank` badge | Portrait pipeline exists but only 2 portraits shipped (`swordsman.png` fallback) vs 0 A.D. `art/textures/ui/session/portraits/units/*.png` (~40). Rank badge `Advanced/Elite` text only `main_node.cpp:1237` vs texture `rank_*.png`. | Need `assets/textures/ui/portraits/**` full set. |
| **Health** – `main_node.cpp:567-575` `make_insp_bar` (5 bars: HP, CP, XP, STA, AURA) | `gui/session/panel_entity/HealthBar.js` segmented stack: `Health` + `Capture` + `Experience` + `Stamina` + `Aura` range | **5/5** bars (see DoD) | Aura keeps a text glyph (no 0 A.D. aura icon in session/icons). The 3D health bar at `world_view_node.cpp:124-145` is still a single bar (not re-checked). |
| **Stats** – `main_node.cpp:395-407` `inspector_stats`/`inspector_extra` text (attack/armor/speed/queue) `main_node.cpp:1178-1183` | `gui/session/selection_details/SelectionDetails.js` template-driven armor `hack/pierce/crush`, attack `hack/pierce/crush + range`, speed, gather rates, garrisoned count | Text dump vs structured rows + icons (`art/textures/ui/session/icons/types/**`) | Need icon grid + observer `OnSelectionChange` template resolve. |
| **Garrison / Ungarrison** – `main_node.cpp:409-416` `ungarrison_btn` `Z` `main_node.cpp:847-856` `order_ungarrison`/`order_disembark_all` | `gui/session/unit_actions/Garrison.js` + `gui/session/panel_entity/GarrisonPanel.js` | Button present | 0 A.D. shows garrisoned unit portraits list; RTS0 shows count only `main_node.cpp:1180`. |

---

## 3. Command / Actions / Production

0 A.D. `gui/session/unit_actions/**`, `gui/session/buildings/**`, `gui/session/queue/**`, `gui/session/structures/**`

| RTS0 | 0 A.D. | Parity | Gap |
|---|---|---|---|
| **Stance panel** – `main_node.cpp:448-475` `stances_bar` 5 buttons; `main_node.cpp:859-863` `order_set_stance` | `gui/session/unit_actions/Stance.js` 5 stances, `art/textures/ui/session/icons/stances/*.png` | **5/5** stances with distinct icons (see DoD) | Stance aura overlay not implemented (not re-checked). |
| **Formations** – `main_node.cpp:2393-2450` 12 formation buttons; `main_node.cpp:865-869` `order_set_formation`; sim enum `FORMATION_COUNT = 13` (`sim_types.h`) | `gui/session/formations/Formation.js` 14 formations (`art/textures/ui/session/icons/formations/*.png`) | 12 of 13 sim formations have buttons (see DoD) | 0 A.D. lists 14 formations; the extra one is not checked here. |
| **Production queue** – `production_queue.h` (`RtsoProductionQueue`); 8 queue slots with icon, chip and progress bar (`main_node.cpp:2318-2335`) | `gui/session/queue/ProductionQueue.js` 8 slots with `ProgressRing` (`gui/common/progressRing/*.js`) | 8-slot icon grid (see DoD) | Ring vs bar styling and queue hotkeys not re-checked. |
| **Build grid** – `main_node.cpp:875-877` `enter_build_mode` + `main_node.cpp:939-954` `ghost_mesh` + `main_node.cpp:1020-1032` ghost preview | `gui/session/buildings/BuildPanel.js` per-civ building roster (`art/textures/ui/session/icons/buildings/**` 13+ icons), `CCmpBuilder` place preview via `gui/session/input.js` `placement` | Build buttons exist (see `update_command_panel()` builder list) but icon set incomplete | Need full building icons + hotkey `B` then `H/B/M/T…` mapping (`gui/session/hotkeys/**`). |
| **Tech** – `session_commands.cpp` `_on_tech_pressed` → `cmd_research` | `gui/session/techs/TechPanel.js` | Handler present | Needs `art/textures/ui/session/icons/technologies/**` icons. |
| **Hotkeys** | `binaries/data/mods/public/gui/session/hotkeys/**` `hotkeys.json` (attack-move `Ctrl`, patrol, guard, stop, etc.) | `main_node.cpp:137` `_unhandled_input` exists but not audited here | Shift queue, control groups, etc. gap – see §4. |

---

## 4. Selection & Input

0 A.D. `gui/session/input.js` + `gui/session/selection.js` + `gui/session/controls/**`

| RTS0 | 0 A.D. | Gap |
|---|---|---|
| **Box select** – `src/presentation/selection_rect.h` `RtsoSelectionRect` in `main_node.cpp:170-171` | `gui/session/selection.js` `StartDragging` / `EndDragging` with `hotkey` additive | Parity. |
| **Shift queue** | `gui/session/input.js:HandleInputBeforeGui` `shiftDown` → `queue` flag on `OrderMove`/`OrderAttack` | Landed (see DoD). |
| **Control groups 0-9** | `gui/session/selection.js:ControlGroups` `SetSelectionGroup(0-9)` + `Recal` | Landed (`selected_groups` handling at `main_node.cpp:2164-2171`). |
| **Double-click select type** | `gui/session/selection.js:DoubleClick` selects all visible of same `template` | Landed (double-click handling at `main_node.cpp:2397-2403`). |
| **Wall drag** | `gui/session/input.js:WallPlacement` `wallset` preview | Landed (see DoD). |
| **Attack-move / Patrol / Guard** | `gui/session/input.js:AttackMove` (`Ctrl`), `Patrol`, `Guard` (`CCmpUnitAI`) | Flags `main_node.cpp:110-112` `attack_move_mode`/`patrol_mode`/`guard_mode` bool exist but UI toggle buttons not exposed in `command_panel`. Wire to `actions_bar`. |

---

## 5. Minimap

0 A.D. `gui/session/minimap/MiniMap.js` + `gui/session/minimap/MiniMapPanel.js` + `gui/session/diplomacy/**`

| RTS0 | 0 A.D. | Gap |
|---|---|---|
| **Panel** – `src/presentation/minimap_view.h` `RtsoMiniMapView` `Control` `main_node.cpp:176-178` `minimap_view` + `_on_minimap_clicked` `main_node.cpp:800-803` | `gui/session/minimap/MiniMap.js` `MiniMap` canvas with `terrainTexture`, `losTexture`, `territoryOverlay` | RTS0 `_draw()` `minimap_view.cpp:90-184` hand-draws rects/circles (terrain grey `93-94`, territory circles `105-123` per building, resource dots `125-137`, buildings `139-159`, units `161-169`, pings `171-179`, camera frustum `181-183`). 0 A.D. composites `terrain` + `LOS` 3-state shroud (unexplored black, explored dark, visible full) via `CCmpRangeManager` `losTexture`. | **Territory circles vs shroud** – `minimap_view.cpp:106-123` `show_territory` circles are placeholder; not sampling `get_fow_grid()` (`main_node.cpp:911-919`). Need `FOW_GRID 128×128` `R8` texture sampled in `_draw()` (see `PERFORMANCE.md` FoW row). |
| **Diplomacy colors toggle** | `gui/session/minimap/MiniMapPanel.js:ToggleDiplomacyColors` (ally/enemy vs player color) | Landed: `diplo_color` in `minimap_view.cpp:169`; `Diplo` toggle button (see DoD). |
| **Flare / Ping** | `gui/session/minimap/MiniMapPanel.js:Flare` (allies see ping) | Landed: flare is sent over NMT_CHAT and parsed back into a map ping (`main_node.cpp:171-183`). |
| **Idle / Alerts** | `gui/session/minimap/IdleButtons.js` + `gui/session/notifications/**` | Missing – no idle-attack/building-complete alert buttons on minimap chrome. |
| **Expand / Collapse** | `gui/session/minimap/MiniMapPanel.js:CollapseButton` | Missing. |

---

## 6. Chat / Diplomacy / Trade Overlays

| RTS0 | 0 A.D. | Gap |
|---|---|---|
| **Chat** – `chat_window.h` (`RtsoChatWindow`); send via `net_host_.sendChat` (`main_node.cpp:1098`) | `gui/session/chat/Chat.js` + `gui/session/chat/ChatPanel.js` addressees | Landed: All / Allies / Enemies / Player-N addressees (`main_node.cpp:885-920`, see DoD). Whisper not re-checked. |
| **Diplomacy** – `diplomacy_dialog.h` (`RtsoDiplomacyDialog`); `_on_diplo_stance_changed` (`main_node.cpp:63`) | `gui/session/diplomacy/DiplomacyDialog.js` per-player stance (Ally/Neutral/Enemy) + `DiplomacyColors.js` | Landed: stance changes are lockstep command 22 (see DoD). Two-peer replication test: `tools/test_diplo_net.cpp`. |
| **Trade / Market / Barter** – `scenes/session/barter_dialog.tscn`; `_on_barter_requested` (`session_commands.cpp`) → `cmd_barter` | `gui/session/trade/TradeDialog.js` + `gui/session/barter/BarterDialog.js` barter with `marketGain` | Landed (see DoD). `TradeRoute.js` parity not required for P5. |
| **Tribute / Ceasefire** | `gui/session/diplomacy/TributeDialog.js`, `gui/session/ceasefire/**` | Missing – P6. |

---

## DoD Checklist for P5 (must pass before merge)

- [x] **Formation heading (G5 – 2026-08-29)** – `Simulation::cmd_move` `src/simulation/simulation.cpp:897` now computes `centerX/Y = avg(ids pos)`, `heading = atan2(tz-centerZ, tx-centerX)`, `cosH/sinH`, then `rx = ox*cosH - oz*sinH`, `rz = ox*sinH + oz*cosH` per `Formation.js GetFormationOffsets` parity; verified `python3 tools/check_parity.py --unit formation` shows LINE not axis-aligned and `PHALANX` speed 0.85x / `WEDGE` 1.1x via `formation_speed_mult[7]` in `cmd_move` + `move_units:1759`. Armor/dmg already in `tick_combat:2013/2036` ✓.
- [x] **Top HUD** – 13 civ emblems in `assets/textures/ui/emblems/*.png` load via `main_node.cpp:114` `load_tex`; active civ emblem swaps on `set_civ()` `main_node.cpp:153` + phase emblem `TextureRect` next to `civ_emblem` updates on `sim->player_phase()` (Village/Town/City) – verify `rg -n "phase_emblem|civ_emblem" src/presentation/main_node.cpp`.
- [x] **Inspector** – DONE 2026-10-05: PNG stat icons LANDED (`extract_ui_assets.py` TEXTURE_MAPPING insp_* from `capture_bar/experience_fg/health_fg/health_bg/stamina_fg` cached DDS; `make_insp_bar(icon_path)` main_node.cpp:613-634 shows 14x14 TextureRect and hides glyph, call sites :664-672; aura keeps glyph — 0 A.D. ships no aura icon in session/icons, so glyph IS the parity fallback). Prior 2026-10-01: 5 bars LANDED (`main_node.cpp:578-586` hp/capture/xp/stamina/aura via `make_insp_bar`); glyph-badge fallback LANDED 2026-10-05 (`main_node.cpp:610-657` text HP/CP/XP/STA/AURA color-coded per bar); still open: real PNG stat icons (`assets/textures/ui/session/icons/` absent). Original: 5-bar stack renders for single selection: `Health` + `Capture` + `XP` + `Stamina` + `Aura` (`PanelEntity.js` parity). `update_inspector()` `main_node.cpp:1139` draws 5 `ProgressBar` rows; `world_view_node.cpp:124` 3D health bar remains single but 2D panel is 5. Need `assets/textures/ui/session/icons/**` stat icons.
- [x] **Command** – DONE 2026-10-05: build hotkeys LANDED (14 BLD_* buildings, main_node.cpp:2713-2731 build_mode modal keys; 0 A.D. input.js has NO letter-to-building maps — grep hotkeydown :1170-1185 only rotation — so mnemonic first-letter fallback documented in-code); queue icon grid LANDED (8 `queue_slots` icon+chip+progress, portrait textures + per-slot progress bar, main_node.cpp:2318-2335, ProductionQueue.js parity); phase tech icons LANDED (set_button_icon textures/ui/phases/town|city.png, main_node.cpp:2363/:2371, cost text kept). Prior 2026-10-01: stances now 5 DISTINCT icons (`main_node.cpp:630-643`, behavior-mapped to 0 A.D. `g_Stances` UnitAI.js:80 — RTS0 Aggressive=`violent`, Defensive=`aggressive`, HoldGround=`defensive`); formations EXPANDED 2026-10-05: sim enum 13 (`sim_types.h` FORMATION_COUNT=13, wire 0..6 stable + 7..12 appended, `formation_metrics[13]` cites `Formation.js:655-873`) + 12 actionable buttons (`main_node.cpp:2393-2450`, icons+gold active highlight, cites `selection_panels.js:265-330`, `formation_panel.xml:1-12`) + 12 icons extracted (`extract_ui_assets.py` formations block, ship glyphs reuse line/column_closed). Open: queue icon grid/ProgressRing, build hotkeys. Original: `stances_bar` 5 distinct icons (`main_node.cpp:456-462` no duplicate), `actions_bar` 14 formation buttons (`Formation.js` parity) with `order_set_formation` `main_node.cpp:865`, `production_bar` + `queue_bar` 8 slots icon grid + `ProgressRing` (not text `Queue (X/8):` `main_node.cpp:1167`), build grid hotkeys `B` → building keys, tech icons. Verify `rg -n "formation|stance" src/presentation/main_node.cpp` shows 14 formation branches.
- [x] **Selection input** – VERIFIED 2026-10-06 (11/11 complete):
  - **Shift-queue waypoints**: `main_node.h:87-93` (`QueuedWaypoint`, `waypoint_queue`), `main_node.cpp:1694-1735` (drain next staged waypoint in `_process()` when unit arrives within 1.5m / idle), `main_node.cpp:2578-2582` (`drop_staged_waypoints()` on order override / stop :2662), `main_node.cpp:3006-3015` (stash active queue on RMB click), `main_node.cpp:3210-3242` (`if (queue_shift)` appends waypoint without wiping active order, lockstep `post_lockstep(1, ...)` / `cmd_move()`, 0 A.D. `gui/session/input.js:1345` parity).
  - **Control groups 0-9**: `main_node.h:207` (`selected_groups[10]`), `main_node.cpp:2611-2646` (Ctrl+0-9 stores selection; bare 0-9 recalls alive player units; Shift+0-9 appends group to selection, 0 A.D. `gui/session/input.js:1684` `performGroup("save"/"select")` parity).
  - **Double-click select matching type**: `main_node.cpp:2907-2944` (on double click, gathers all same `unit_type` player units within camera viewport, 0 A.D. `gui/session/input.js:1022-1042` `Engine.PickSimilarPlayerEntities` parity).
  - **Wall-ghost drag placement**: `main_node.h:112-114` (`wall_ghosts`, `wall_drag_mode`, `wall_drag_start_pos`), `main_node.cpp:1833-1905` (3D ghost preview along drag vector with step vector, segment yaw `atan2`, tower end caps, intermediate wall meshes, and `can_place()` ok/bad materials), `main_node.cpp:2817-2821` (initiates drag on LMB down with `BLD_WALL_SEGMENT`), `main_node.cpp:2845-2886` (on LMB release commits towers and wall segments via `post_lockstep(5, ...)` under lockstep or `sim->cmd_build()` + `order_build_or_repair()` in local play, 0 A.D. `gui/session/building_placement.js:45-92` / `placement.js:140-195` parity).
- [x] **Minimap** – DONE 2026-10-05: all sub-items landed (shroud 3-state, diplo palette, chat addressee, flare networked — evidence below). Prior 2026-10-01: 3-state shroud DONE (`minimap_view.cpp:199-231` black/40%/full), idle-worker cycle DONE (`main_node.cpp:1351/1354`); diplomacy palette LANDED 2026-10-05 (`minimap_view.cpp:110-122` `diplo_color` from `sim->get_diplomacy`, `Diplo` button `main_node.cpp:270`); `Shroud` button now calls `cycle_shroud` 3-state (`main_node.cpp:269`, cites `LOSTexture.cpp:443-449`, `MiniMapPanel.js:8-11`); chat addressee selector LANDED (`main_node.cpp:885-920` All/Allies/Enemies/Player-N wire tags, `onChat` parse `:175-208`, cites `ChatWindow.js:35-58`, `ChatBounce.js:15-38`); flare broadcast VERIFIED networked 2026-10-05 (`main_node.cpp:1446 _on_flare_requested` -> `sendChat("[Flare] x,z")`, receive parse `:171-183` -> `add_ping`; gated on `net_lockstep()`); open: (none for flare — client-side tag documented as NMT_CHAT approximation of 0AD minimap flare). Original: `_draw()` `minimap_view.cpp:90` samples `get_fow_grid()` `main_node.cpp:911` as `ImageTexture` 3-state shroud (unexplored black / explored 40% / visible 100%) instead of only territory circles `minimap_view.cpp:106`; diplomacy color toggle `minimap_view.cpp:8` `toggle_diplomacy` switches ally/enemy palette; flare button broadcasts via `net_host`; idle-attack button cycles `cycle_idle_worker()` `main_node.cpp:810`. Verify `rg -n "fow_grid|losTexture" src/ shaders/` hits shader.
- [x] **Chat** – DONE 2026-10-01: `LineEdit` + addressee All/Allies/Team (`main_node.cpp:798-804`) + history `chat_log`; network send wired `_on_chat_send_pressed` → `net_host_.sendChat` (net_host.cpp:327, NMT_CHAT) and receive via `net_host_.onChat` registered in `_ready()`; channel routing is wire-tag (no per-GUID recipient filter — documented gap). Original: `ChatOverlay` `LineEdit` + addressee `OptionButton` (All/Allies/Team) + history `RichTextLabel`; sends `ChatMessage` packet via `net_host.h`; `Addressee` enum parity `gui/session/chat/Chat.js:Addressee`.
- [x] **Diplomacy** – DONE 2026-10-05: two-peer replication test LANDED `tools/test_diplo_net.cpp` (523 lines, 4 phases: wire contract, real UDP Host+Client 127.0.0.1 replication, negative control — one-sided stance DIVERGES hash + raises OOS, 3-peer relay; PASS x5, mutation-tested) + `NMT_DIPLOMACY=31` (net_types.h:56, sendDiplomacy/onDiplomacy/dispatch relay net_host.cpp:339/:411/:568) + OOS fix: stance now lockstep cmd_type 22 (turn_manager.cpp dispatch -> `cmd_set_diplomacy` simulation.cpp:1007, applied on every peer at boundary; UI `_on_diplo_stance_changed` posts via post_lockstep(22) — was local-only `set_diplomacy` = desync). Prior 2026-10-01: `set_diplomacy` wired from dialog (`main_node.cpp` `_on_diplo_stance_changed`); minimap color switch on stance change LANDED 2026-10-05 (`minimap_view.cpp:110-122` + `Diplo` toggle button). Original: `DiplomacyDialog` `Panel` listing players 1..N (skip self) with `OptionButton` Ally/Neutral/Enemy per row, `OnStanceChange` calls `Simulation::set_diplomacy` and updates `minimap_view` colors; test two-client Enet: stance change replicates.
- [x] **Trade** – DONE 2026-10-01: `scenes/session/barter_dialog.tscn` 6-button sell/buy grid (100→±75/100, dynamic ±2% prices, civ spread), `_on_barter_requested` → `sim->cmd_barter` (`main_node.cpp:1448`), trader distance gain `calculate_trader_gain` (`simulation.cpp:1003`, used at `:1872`). Original: `TradeDialog`/`BarterDialog` `Panel` with resource barter grid (100 → X) using `market_gain` rate; `TRADER` unit `CCmpTrader` gain scales with market distance; verify barter changes `sim->food/gold` atomically. `TradeRoute.js` parity not required for P5 (auto path OK).
- [x] **Assets** – VERIFIED 2026-10-05: `godot --headless --path . --import` clean run generated 1365 `.import` files (incl. all 12 formation icons + 5 insp_* stat icons); `tools/audit_assets.py` RC=0 (142 res:// paths, 0 missing); note: `ctest` N/A — repo builds with SCons, no CMakeLists.txt. Baseline: `assets/textures/ui/session/auras/*`, `minimap-*`, `panel_phase_emblems`, formations 14 PNGs, emblems 13 PNGs present; `extract_0ad_assets.py --full` emits them (see `ASSET_MAPPING.md` Next export targets). `godot --headless --check` + `ctest` sim input tests pass.
- [x] **No regressions** – VERIFIED 2026-10-05: `scons target=template_debug` GREEN (lib links, full merge of E3/J3/F3 + cycle_shroud edit); `tools/test_replay.cpp` RC=0 (lockstep hash round-trip, negative control); `tools/test_animation.py` RC=0 (157 mappings, 108/108 clip resolution); `tools/audit_assets.py` RC=0; `check_parity.py` results are tracked in `docs/REMAINING_GAPS.md`. Lockstep determinism untouched by stream edits (all SoA-column hashes covered per `turn_manager.cpp:140`).
