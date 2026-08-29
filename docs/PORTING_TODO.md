# 0 A.D. to RTS0 Porting Roadmap & Master TODO

This document tracks all remaining gameplay systems, UI elements, audio assets, and engine features to be ported from **0 A.D.** (`/home/abhi/project/0ad`) into **RTS0** (`/home/abhi/project/RTS0`).

Each item is categorized by subsystem, prioritized, and linked to its 0 A.D. source reference and RTS0 target implementation.

---

## 📋 Table of Contents
1. [Phase 1: Top Bar & Session HUD](#phase-1-top-bar--session-hud)
2. [Phase 2: Selection Card & Inspector Panel](#phase-2-selection-card--inspector-panel)
3. [Phase 3: Action Panel, Command Grid & Stances](#phase-3-action-panel-command-grid--stances)
4. [Phase 4: Production Queue, Rally Points & Training](#phase-4-production-queue-rally-points--training)
5. [Phase 5: Construction, Multi-Builder & Repair](#phase-5-construction-multi-builder--repair)
6. [Phase 6: Garrisoning & Turret Systems](#phase-6-garrisoning--turret-systems)
7. [Phase 7: Technology & Upgrade Tree](#phase-7-technology--upgrade-tree)
8. [Phase 8: Economy, Barter & Market Trading](#phase-8-economy-barter--market-trading)
9. [Phase 9: Tactical Minimap, Alerts & Territory](#phase-9-tactical-minimap-alerts--territory)
10. [Phase 10: In-Game Dialogs & Modals](#phase-10-in-game-dialogs--modals)
11. [Phase 11: Post-Game Match Summary Screen](#phase-11-post-game-match-summary-screen)
12. [Phase 12: Sound Effects, Voice Lines & Audio Cues](#phase-12-sound-effects-voice-lines--audio-cues)
13. [Phase 13: Combat Physics, Projectiles & Formations](#phase-13-combat-physics-projectiles--formations)
14. [Phase 14: Mobile & Touch Control Interface](#phase-14-mobile--touch-control-interface)
15. [Phase 15: Multiplayer Sync & Network TurnManager](#phase-15-multiplayer-sync--network-turnmanager)

---

## Phase 1: Top Bar & Session HUD
> **0 A.D. Source Reference:** `gui/session/TopPanel.xml`, `gui/session/top_panel/`, `art/textures/ui/session/`  
> **RTS0 Target:** `native/src/presentation/main_node.cpp`, `scenes/session/top_panel.tscn`

- [x] **1.1 Civ Crest & Player Identity Banner**
  - [x] Add civilization emblem (Athenian owl / Spartan lambda) extracted from 0 A.D. UI textures.
  - [x] Add player name & team color indicator badge in top panel corner.
- [x] **1.2 Styled 0 A.D. Resource Counters**
  - [x] Extract and display official 0 A.D. resource icons:
    - 🌾 Food (`art/textures/ui/session/icons/resources/food.png`)
    - 🌲 Wood (`art/textures/ui/session/icons/resources/wood.png`)
    - 🪨 Stone (`art/textures/ui/session/icons/resources/stone.png`)
    - 🪙 Metal / Gold (`art/textures/ui/session/icons/resources/metal.png`)
  - [x] Show current resource amounts with thousand separators (`1,250`).
  - [x] Show net resource trickle / gather rates per minute (`+140/m`).
- [x] **1.3 Population & Idle Worker Tracker**
  - [x] Display Population fraction (`Current / Max Pop`) with pop cap warning color when capped.
  - [x] Add **Idle Worker Button** showing idle villager count.
  - [x] Implement Idle Worker click handler and hotkey (`.` or `Tab`) to cycle camera focus through idle villagers.
- [x] **1.4 Match Clock, Game Speed & Quick Menu Controls**
  - [x] Match elapsed timer (`MM:SS`).
  - [x] Game speed selector button (`0.5x`, `1.0x`, `1.5x`, `2.0x`, `Pause`).
  - [x] Quick buttons for In-Game Menu (`Esc`), Diplomacy Dialog, and Help/Encyclopedia.

---

## Phase 2: Selection Card & Inspector Panel
> **0 A.D. Source Reference:** `gui/session/PanelEntities.xml`, `gui/session/selection_details/`, `CCmpHealth.cpp`  
> **RTS0 Target:** `native/src/presentation/main_node.cpp`, `native/src/presentation/inspector_view.h/.cpp`

- [x] **2.1 Single Entity Portrait & Identity**
  - [x] Render high-quality entity portrait/icon based on selected unit/building type.
  - [x] Display full unit name, historical title, and rank badge (e.g. *Athenian Hoplite - Citizen Soldier*).
  - [x] Show kill counter and current unit experience/rank progress.
- [x] **2.2 Visual Health & Armor Bars**
  - [x] Render 0 A.D. styled segmented health bar with HP numerical readout (`160 / 160`).
  - [x] Dynamic color gradient based on health percentage (Green $\ge$ 50%, Yellow 25-50%, Red $<$ 25%).
  - [x] Armor breakdown icons:
    - ⚔️ Hack Resistance (`hack_armor`)
    - 🏹 Pierce Resistance (`pierce_armor`)
    - 🔨 Crush Resistance (`crush_armor`)
- [x] **2.3 Combat & Movement Stats**
  - [x] Display attack damage type, attack range (melee vs ranged), and repeat rate (attacks/sec).
  - [x] Display movement speed and carry capacity for gatherers.
  - [x] Show current order state (e.g. *Gathering Wood from Pine*, *Attacking Spartan Raider*, *Moving to Waypoint*).
- [x] **2.4 Multi-Selection Grouping & Filtering**
  - [x] When multiple units are selected, group them by unit type with sub-count chips (e.g., `[14x Hoplites]`, `[8x Archers]`, `[6x Villagers]`).
  - [x] Clicking a unit chip selects only that subgroup.
  - [x] Double-clicking or Shift-clicking filters the selection.

---

## Phase 3: Action Panel, Command Grid & Stances
> **0 A.D. Source Reference:** `gui/session/selection_panels_left/`, `UnitAI.js`, `Formation.js`  
> **RTS0 Target:** `native/src/presentation/main_node.cpp`, `native/src/simulation/simulation.cpp`

- [x] **3.1 Unit Stances Sub-Panel**
  - [x] Add 0 A.D. stance icons:
    - 🔴 **Aggressive** (`aggressive.png`): Pursues enemies on sight within vision range.
    - 🟡 **Defensive** (`defensive.png`): Attacks enemies that approach, returns to post.
    - 🔵 **Stand Ground** (`standground.png`): Attacks targets in range without moving.
    - ⚪ **Hold Ground** (`violent.png`): Short pursuit only.
    - 🟢 **Passive** (`passive.png`): Flee from attackers, never attack.
  - [x] Wire stance toggle buttons and hotkeys (`F1`-`F5`).
- [x] **3.2 Unit Formations Sub-Panel**
  - [x] Add 0 A.D. formation icons:
    - Line (`battle_line.png`)
    - Box (`box.png`)
    - Column (`column_closed.png`)
    - Phalanx / Syntagma (`phalanx.png`)
    - Wedge (`wedge.png`)
    - Skirmish / Scatter (`scatter.png`)
  - [x] Implement formation positioning math in `native/src/simulation/simulation.cpp`.
- [x] **3.3 Standard Unit Commands Grid**
  - [x] Stop Order (`S` / button) — cancels current path and orders.
  - [x] Hold Position (`H` / button) — holds position deterministically.
  - [x] Patrol Order (`P` + click) — moves back and forth along path.
  - [x] Guard Order (`G` + click) — escorts and defends target unit or structure.
  - [x] Delete / Disband Unit (`Delete` key with confirmation prompt).

---

## Phase 4: Production Queue, Rally Points & Training
> **0 A.D. Source Reference:** `ProductionQueue.js`, `RallyPoint.js`, `gui/session/unit_training.xml`  
> **RTS0 Target:** `native/src/simulation/simulation.cpp`, `native/src/presentation/main_node.cpp`

- [x] **4.1 Production Action Buttons & Tooltips**
  - [x] Building-specific training buttons with cost breakdown tooltips (Food, Wood, Gold, Pop requirement, Training time).
  - [x] Support batch training (`Shift+Click` for +5 units).
  - [x] Disable / grey out buttons when resources or population cap are insufficient.
- [x] **4.2 Visual Multi-Slot Production Queue**
  - [x] Render 0 A.D. queue slots displaying icon of unit currently in training + pending queue.
  - [x] Radial or linear timer progress bar showing completion percent.
  - [x] Right-click queue item to cancel training and refund resources 100%.
- [x] **4.3 Rally Point System**
  - [x] Right-click anywhere while a production building is selected to place a **Rally Point**.
  - [x] Render visual 3D Rally Point flag marker on terrain.
  - [x] Automatically issue move order to newly spawned units towards the rally point.
  - [x] If rally point is set to a resource (tree/mine) or unfinished building, newly trained villagers auto-gather/auto-build upon spawning.

---

## Phase 5: Construction, Multi-Builder & Repair
> **0 A.D. Source Reference:** `Foundation.js`, `Builder.js`, `Repairable.js`, `BuildRestrictions.js`  
> **RTS0 Target:** `native/src/simulation/simulation.cpp`, `native/src/presentation/main_node.cpp`

- [x] **5.1 0 A.D. Construction Grid Menu**
  - [x] Build sub-menu categorized into:
    - **Civic / Eco:** House, Farmstead, Storehouse, Mill, Market.
    - **Military:** Barracks, Archery Range, Stable, Fortress, Defense Tower, Outpost.
    - **Fortifications:** Stone Wall, Wooden Palisade, Wall Tower, Gate.
  - [x] Visual ghost placement mesh with green (valid) / red (obstructed) placement feedback.
- [x] **5.2 Multi-Worker Collaborative Foundation Construction**
  - [x] Foundation placed with starting initial health (10% max HP).
  - [x] Up to $N$ villagers can work on the same foundation simultaneously.
  - [x] Construction rate scales according to 0 A.D. formula ($Rate = \sqrt{N}$ or linear diminishing returns).
  - [x] Play building scaffolding/under-construction visual progression.
- [x] **5.3 Repair Mechanics**
  - [x] Right-click damaged friendly buildings with selected workers to initiate repair.
  - [x] Repair consumes fractional resources (wood/stone) per second until HP is fully restored.
  - [x] Visual repair tool animation (hammering).

---

## Phase 6: Garrisoning & Turret Systems
> **0 A.D. Source Reference:** `GarrisonHolder.js`, `Garrisonable.js`, `TurretHolder.js`  
> **RTS0 Target:** `native/src/simulation/simulation.cpp`, `native/src/presentation/main_node.cpp`

- [x] **6.1 Unit Garrisoning**
  - [x] Right-click friendly Civic Center, Defense Tower, or Fortress to garrison infantry/archers inside.
  - [x] Capacity limits: Town Center (20 slots), Tower (5 slots), Fortress (30 slots).
  - [x] Garrisoned units are protected from all melee and ranged damage while inside.
- [x] **6.2 Defensive Arrow Boost**
  - [x] Each garrisoned ranged/infantry unit adds additional ballistic arrow volleys to the building's defensive attack.
- [x] **6.3 Garrison UI & Ungarrison Controls**
  - [x] Garrison panel on building selection showing icons of all units inside.
  - [x] "Eject All" / "Ungarrison" button (`Z` hotkey) to exit units near the building entrance.
  - [x] Click individual garrisoned unit portrait to eject only that specific unit.

---

## Phase 7: Technology & Upgrade Tree
> **0 A.D. Source Reference:** `TechnologyManager.js`, `Upgrade.js`, `Researcher.js`  
> **RTS0 Target:** `native/src/simulation/components/technology_manager.h`, `native/src/simulation/simulation.cpp`

- [x] **7.1 Civilization Phase Upgrades**
  - [x] **Phase 1 (Village Phase):** Starting tier.
  - [x] **Phase 2 (Town Phase):** Researched at Town Center (unlocks Barracks, Towers, Markets, advanced tech).
  - [x] **Phase 3 (City Phase):** Unlocks Fortresses, Siege Engines, elite civic upgrades.
- [x] **7.2 Economic Technologies**
  - [x] Farming tools (+20% food gather rate).
  - [x] Iron axes (+20% wood gather rate).
  - [x] Shaft mining (+20% gold/stone gather rate).
  - [x] Wheelbarrow / Handcart (+50% villager carry capacity & movement speed).
- [x] **7.3 Military Technologies**
  - [x] Bronze / Iron Weapons (+15% melee & ranged damage).
  - [x] Scale / Plate Armor (+2 hack/pierce armor).
  - [x] Archery fletching (+20% bow range and projectile speed).
- [x] **7.4 Research UI Panel**
  - [x] Tech research icons with progress timers displayed inside building inspector cards.

---

## Phase 8: Economy, Barter & Market Trading
> **0 A.D. Source Reference:** `Market.js`, `Barter.js`, `Trader.js`, `gui/session/selection_panels_left/barter_panel.xml`  
> **RTS0 Target:** `native/src/simulation/components/market.h`, `scenes/session/barter_dialog.tscn`

- [x] **8.1 Barter Exchange System**
  - [x] Dynamic buy/sell rates for Food, Wood, Stone, and Metal.
  - [x] Trade 100 of resource $A$ for resource $B$ with 20% market fee.
  - [x] Market prices fluctuate dynamically based on transaction supply and demand.
- [x] **8.2 Barter UI Dialog**
  - [x] Interactive modal dialog with Quick Buy / Quick Sell 100 and 500 unit buttons.
- [x] **8.3 Trade Routes - [ ] **8.3 Trade Routes & Caravans** Caravans**
  - [x] Train Merchant Caravans from Market.
  - [x] Establish trade routes between friendly Markets for continuous passive gold generation.

---

## Phase 9: Tactical Minimap, Alerts & Territory
> **0 A.D. Source Reference:** `MiniMap.xml`, `MiniMapTexture.cpp`, `TerritoryManager.cpp`, `Los.h`  
> **RTS0 Target:** `native/src/presentation/minimap_view.cpp`, `native/src/simulation/components/territory_manager.h`

- [x] **9.1 Territory Influence Borders**
  - [x] Compute civilization territorial borders influenced by Civic Centers, Towers, and Fortresses.
  - [x] Render colored territory border outlines on minimap and 3D terrain.
  - [x] Territory Decay: Neutral/Enemy buildings outside territory slowly lose health over time.
- [x] **9.2 Fog of War & Line of Sight (LOS)**
  - [x] 3-tier visibility: Unexplored (black), Explored/Fogged (greyscale/dim), Visible (live updates).
  - [x] RangeManager LOS calculation for units and buildings.
  - [x] Minimap fog overlay texture.
- [x] **9.3 Minimap Interactive Buttons & Pings**
  - [x] Minimap flare / ping tool (alerts allies to attack/defend location).
  - [x] Toggle buttons: Resource icons visibility, Territory layer, Military vs Economy blips.

---

## Phase 10: In-Game Dialogs & Modals
> **0 A.D. Source Reference:** `gui/session/diplomacy/`, `gui/session/dialogs/`, `gui/session/Menu.xml`  
> **RTS0 Target:** `scenes/session/`, `scenes/menu/`

- [x] **10.1 Diplomacy - [ ] **10.1 Diplomacy & Tribute Window** Tribute Window**
  - [x] List all players and AI factions with team, civ icon, and current diplomatic state (Ally / Neutral / Enemy).
  - [x] Tribute buttons to send Food, Wood, Gold, Stone to allies (with 100 / 500 increments).
  - [x] Toggle stance buttons (Ally / Enemy / Ceasefire request).
- [x] **10.2 In-Game Settings & Keybinding Picker**
  - [x] Full hotkey customization screen ported from 0 A.D. `data/config/keys.txt`.
  - [x] Audio volume sliders (Master, Music, SFX, Ambient, Voice lines).
  - [x] Video settings (Resolution, Fullscreen, SSAO, Shadows, MultiMesh density).
- [x] **10.3 In-Game Chat Overlay**
  - [x] Multiplayer/AI chat window (`Enter` to type).
  - [x] Team chat / All chat channels.

---

## Phase 11: Post-Game Match Summary Screen
> **0 A.D. Source Reference:** `gui/summary/`, `StatisticsTracker.js`  
> **RTS0 Target:** `scenes/session/summary_screen.tscn`, `scenes/session/summary_screen.gd`

- [x] **11.1 Scoreboard Overview**
  - [x] Final victory / defeat status banner with civ crests.
  - [x] Total overall score calculated from Economy + Military + Tech + Buildings.
- [x] **11.2 Economic Statistics Tab**
  - [x] Total food, wood, stone, gold gathered throughout match.
  - [x] Peak gather rates per minute.
  - [x] Total market trades executed.
- [x] **11.3 Military Statistics Tab**
  - [x] Total units trained, units lost, enemies killed.
  - [x] Kill/Death ratio (K/D).
  - [x] Total buildings constructed vs buildings destroyed.
- [x] **11.4 Timeline & Progress Chart**
  - [x] Line graph tracking Population, Military Size, and Total Score over match time.

---

## Phase 12: Sound Effects, Voice Lines & Audio Cues
> **0 A.D. Source Reference:** `audio/interface/`, `audio/voice/`, `audio/actor/`, `audio/destruction/`  
> **RTS0 Target:** `assets/sounds/`, `native/src/presentation/main_node.cpp`

- [x] **12.1 UI Sound Effects**
  - [x] Button hover and click sounds (`interface/button_click.ogg`).
  - [x] Selection click SFX for units and buildings.
  - [x] Building placement confirmation sound and cancel sound.
  - [x] Invalid order / insufficient resource buzzer.
- [x] **12.2 Greek & Hellenic Unit Voice Lines**
  - [x] Extract Hellenic unit acknowledgement voices (`audio/voice/hellenes/`):
    - Selection voices (*"Khaire"*, *"Ti estin"*, *"Hetoimos"*).
    - Move orders (*"Poreuomai"*, *"Baino"*).
    - Attack orders (*"Epi tous polemious!"*, *"Eis makhen!"*).
    - Gather / Build orders (*"Ergon"*, *"Poieso"*).
- [x] **12.3 Combat & Destruction SFX**
  - [x] Sword clang / slash melee hit sounds.
  - [x] Bow string release & arrow impact thump.
  - [x] Building fire, collapse, and debris rubble audio.
- [x] **12.4 Event Alarms & Notifications**
  - [x] Horn alarm: *"Our base is under attack!"* (`alarm_attacked.ogg`).
  - [x] Fanfare: *"Research completed"* / *"Phase upgrade finished"*.
  - [x] Victory anthem & Defeat lament tracks.

---

## Phase 13: Combat Physics, Projectiles & Formations
> **0 A.D. Source Reference:** `Attack.js`, `CCmpUnitMotion.cpp`, `FormationAttack.js`  
> **RTS0 Target:** `native/src/simulation/simulation.cpp`, `native/src/presentation/world_view_node.cpp`

- [x] **13.1 Ballistic Projectile Physics**
  - [x] Parabolic flight arcs for arrows, javelins, and catapult boulders ($y(t) = 4h \cdot t(1-t)$).
  - [x] Trajectory lead calculation for moving targets.
  - [x] Miss chance / dispersion spread on long-distance shots.
  - [x] Visual arrow 3D meshes rendered via instanced batch with directional alignment.
- [x] **13.2 Unit Counters & Damage Multipliers**
  - [x] Spearman +100% bonus damage vs Cavalry.
  - [x] Cavalry +50% bonus damage vs Archers and Siege.
  - [x] Archers +30% bonus damage vs lightly armored Infantry.
  - [x] Siege Rams +300% bonus damage vs Stone Walls and Fortresses.
- [x] **13.3 Formation Movement Physics**
  - [x] Units maintain assigned grid slot positions while moving as a company.
  - [x] Wheel turning and column reformation when changing directions.

---

## Phase 14: Mobile & Touch Control Interface
> **0 A.D. Source Reference:** `source/config/android/`, `docs/MULTIPLAYER.md`  
> **RTS0 Target:** `native/src/presentation/camera_rig.cpp`, `scenes/session/touch_controls.tscn`

- [x] **14.1 Gesture Navigation**
  - [x] Single finger pan terrain.
  - [x] Two finger pinch-to-zoom and twist-to-rotate camera rig.
  - [x] Double-tap ground to quickly center camera.
- [x] **14.2 Touch Action HUD Overlay**
  - [x] Virtual radial command wheel for mobile touch screens.
  - [x] Touch box-selection toggle button.
  - [x] Large touch-friendly bottom action icons with haptic feedback.

---

## Phase 15: Multiplayer Sync & Network TurnManager
> **0 A.D. Source Reference:** `source/network/`, `NetServerTurnManager.cpp`, `NetClientTurnManager.cpp`  
> **RTS0 Target:** `native/src/0ad/network/`, `native/src/network/turn_manager_net.h`

- [x] **15.1 Deterministic Command Lockstep Replay**
  - [x] Binary serialization of `GameCommand` packets.
  - [x] TurnManager tick sync at 20Hz across host and connected clients.
  - [x] HashSerializer frame hash verification for out-of-sync (OOS) detection.
- [x] **15.2 In-Game Multiplayer Lobby**
  - [x] ENet socket connection handling.
  - [x] Map, team, faction, and handicap selection in lobby.
  - [x] Player latency / ping indicators.

---


---

## Phase 16: Specialized 0 A.D. Mechanics (Native C++ Engine)
> **0 A.D. Source Reference:** `Promotion.js`, `Heal.js`, `Auras.js`, `Wonder.js`, `Treasure.js`  
> **RTS0 Target:** `native/src/simulation/simulation.cpp`, `native/src/core/memory/soa_storage.h`

- [x] **16.1 Promotion & Veterancy System (`Promotion.js`)**
  - [x] Unit kill tracking (`kills`).
  - [x] Rank 2 Advanced promotion (3 kills) granting +15% HP, +10% Damage with rank title.
  - [x] Rank 3 Elite promotion (7 kills) granting +30% HP, +25% Damage with star badges (`★`, `★★`).
- [x] **16.2 Temple Healing & Regeneration Fields (`Heal.js`)**
  - [x] Temple building (`BLD_TEMPLE`) radiating +3.5 HP/s healing aura to all friendly units within 18m.
- [x] **16.3 Civic & Monument Inspiration Fields (`Auras.js`)**
  - [x] Town Centers and Fortresses project command and defensive protection bonuses within territory circles.
- [x] **16.4 Wonder Monument & Victory Countdown (`Wonder.js`)**
  - [x] Wonder monument (`BLD_WONDER`) triggering 10-minute Sudden Death Wonder Victory timer.
- [x] **16.5 Wilderness Relics & Treasure Troves (`Treasure.js`)**
  - [x] Procedural treasure caches across the wilderness providing instant +150-200 gold/wood/food/stone upon scout collection.


---

## Phase 17: Historical Champions & Heroes (`Hero.js` / `Auras.js`)
- [x] **17.1 Champion Hero Unit (`UNIT_HERO`)**
  - [x] Elite combat stats (650 HP, 45 DMG, 6.0 Hack/Pierce Armor).
  - [x] Spartan Leonidas passive combat healing aura & Athenian Pericles movement speed aura.
  - [x] Trainable at Barracks in Town/City phase.

---

## Phase 18: Dynamic Structure & Entity Capture (`Capturable.js`)
- [x] **18.1 Capture Points System (`Capturable.js`)**
  - [x] Capture points gauge on buildings (`capture_points` / `max_capture_points`).
  - [x] Undefended enemy structures captured by nearby friendly units over time.
  - [x] Automatic ownership transfer to player when capture points reach max.

---

## Phase 19: Naval Warfare & Coastal Fleet Expansion
- [x] **19.1 Naval Shipyard Dock (`BLD_DOCK`)**
  - [x] Coastal building constructed by villagers.
- [x] **19.2 War Trireme Vessel (`UNIT_TRIREME`)**
  - [x] Heavy naval warship (800 HP, 35 DMG, 20m range, 8.0 Pierce armor).

---

## Phase 20: Defensive Stone Wall Fortifications (`WallPiece.js`)
- [x] **20.1 Stone Wall Segment (`BLD_WALL_SEGMENT`)**
  - [x] High durability (1800 HP) fortification segment.


---

## Phase 21: Tactical Commands & Minimap Ping Flares
- [x] **21.1 Tactical Minimap Alert Flares (`Alt + Click`)**
  - [x] Sends visual radiating ping flare and acoustic alert chime on terrain / minimap.
- [x] **21.2 Cinematic HUD Toggle (`F11`)**
  - [x] Clean screenshot and battle immersion mode.


---

## Phase 22: Antiquity War Elephants & Attack-Move/Patrol Tactics
- [x] **22.1 Carthaginian War Elephant (`UNIT_ELEPHANT`)**
  - [x] Massive juggernaut (1,200 HP, 65 DMG, 8.0 Pierce armor, 3.0 Crush).
  - [x] Trainable in Fortress/Barracks for Carthaginians & City Phase.
- [x] **22.2 Tactical Commands (`ACTION_ATTACK_MOVE` & `ACTION_PATROL`)**
  - [x] Attack-Move order (`Q + RMB`): Aggressive movement engaging all hostiles in path.
  - [x] Patrol order (`P + RMB`): Continuous boundary patrolling with defensive stance.


---

## Phase 23: Complete Mod (`public.zip`) Component & Mechanics Port
- [x] **23.1 Spoils of War Plunder & Salvage (`Loot.js` & `Looter.js`)**
  - [x] Slaying enemy units awards direct battlefield resource plunder (+8 Gold, +6 Food).
- [x] **23.2 Siege Catapult Area-of-Effect Blast (`Damage.js` / Splash)**
  - [x] Catapult rock impacts deal 4.5m radius blast damage (60% splash) to surrounding infantry squads.
- [x] **23.3 Diminishing-Returns Multi-Worker Construction (`Builder.js`)**
  - [x] Native multi-villager build scaling: sqrt(N)/N with Roman rapid construction trait.


---

## Phase 24: Save & Load Match State Serialization (`SavedGames.js`)
- [x] **24.1 Binary Match Snapshot Serialization (`save_state` & `load_state`)**
  - [x] Pure C++ binary stream serialization of units SoA, buildings, production queues, technologies, resources, and match timers.
- [x] **24.2 In-Game Pause Menu Save/Load Integration**
  - [x] "💾 Save Game" and "📂 Load Game" buttons in `scenes/session/pause_menu.tscn`.


---

## Phase 25: Vision/LOS (`VisionShared.js`), Guarding (`Guard.js`), Income Trickle (`ResourceTrickle.js`) & Autonomous Bot Macro (`Petra.js`)
- [x] **25.1 Fog of War (FoW) & Line of Sight Grid (`VisionShared.js` & `RangeManager.js`)**
  - [x] 64x64 dynamic FoW grid over the battlefield with state tracking (`FOW_UNEXPLORED`, `FOW_EXPLORED`, `FOW_VISIBLE`).
  - [x] Variable unit vision radii (Triremes 28m, Catapults 24m, Archers 22m, Cavalry/Elephants 20m, Heroes 18m, Infantry 16m) and building observation radii.
  - [x] Bound to Godot via `get_fow_grid()`.
- [x] **25.2 Bodyguard Escort & Target Defense (`Guard.js`)**
  - [x] `ACTION_GUARD` tactical order with dynamic leash/escort distance tracking (3m tether).
  - [x] Bound hotkey `G` and `order_guard_units(target_id)`.
- [x] **25.3 Passive Resource Trickle (`ResourceTrickle.js`)**
  - [x] Autonomous municipal revenues from civic centers, farmsteads, markets, and wonders.
- [x] **25.4 Petra Autonomous AI Macro Strategy (`Petra.js`)**
  - [x] Real-time macro-economic progression (early, mid, late phase transitions).
  - [x] Automated production scheduling and military squad dispatch towards player settlements.

## Phase 26: Multi-Civilization Factions & Faction Bonuses
- [x] **26.1 Six Historical Civilizations (`CivInfo`)**
  - [x] Athenians (+15% metal mining rate, owl emblem).
  - [x] Spartans (+35% HP, +25% melee damage, lambda shield emblem).
  - [x] Romans (+30% construction speed, legionary armor, eagle emblem).
  - [x] Persians (+25% barter trade gains, faster cavalry training, lion emblem).
  - [x] Carthaginians (+10% barter spread, War Elephants).
  - [x] Macedonians (Sarissa phalanx combat bonuses).
- [x] **26.2 Real-Time Top HUD Civ Selector**
  - [x] Dynamic faction selector with emblem swap and instant trait activation.

---

## Phase 27: Fauna, Wildlife & Hunting System (`Animal.js` & `Pack.js`)
- [x] **27.1 Wildlife Entities**
  - [x] Red Deer, Wild Boar, Mountain Sheep, Gray Wolf.
- [x] **27.2 Animal AI Behaviors**
  - [x] Passive grazing and random slow wander.
  - [x] Passive game fleeing from player/enemy units within 12m.
  - [x] Predator territory defense and melee attacks against nearby units.
- [x] **27.3 Carcass & Meat Harvesting**
  - [x] Conversion of dead fauna into food nodes with 1.5x villager harvest speed.

---

## Phase 28: Interactive Spline Wall & Gate Builder (`WallPiece.js` & `Gate.js`)
- [x] **28.1 Click-and-Drag Wall Builder**
  - [x] Continuous stone wall layout with automatic corner/node tower snapping (<3.5m).
- [x] **28.2 Wall-to-Gate Upgrades**
  - [x] Ctrl-Right-Click upgrade of completed wall segment into a fortress Gate (`BLD_GATE`).

---

## Phase 29: High-Ground Elevation Combat Advantage (`RangeManager.cpp` & `Attack.js`)
- [x] **29.1 Elevation Tactical Modifiers**
  - [x] Up to +50% attack range extension for units on cliffs and elevated terrain.
  - [x] +25% high-ground damage multiplier against units in lower valleys.
  - [x] -20% uphill attack penalty.

---

## Phase 30: Adaptive Music Crossfader & Siege Battering Rams
- [x] **30.1 Dynamic Music Crossfader (`Music.js`)**
  - [x] Seamless cross-fading between peace ambient soundtrack and battle music (`battle_1.ogg`) based on live viewport combat intensity.
- [x] **30.2 Siege Battering Rams (`UNIT_SIEGE_RAM`)**
  - [x] Heavy siege unit with 12.0 Pierce armor and 4x crush damage multiplier vs stone gates and fortresses.

---

## 🎯 Porting Progression Log

| Date | Phase / Item | Description | Status |
|---|---|---|---|
| **2026-08-27** | **Math & SoA Sim** | Ported Fixed 15.16, Grid, Hierarchical/Long Pathfinder, SoA Storage | ✅ Completed |
| **2026-08-27** | **3D Asset Bake** | 24 GLBs extracted from 0 A.D. with PMD/PSA skeletal animation support | ✅ Completed |
| **2026-08-27** | **UI & HUD Port** | Full 0 A.D. Top Bar, Inspector, Stances, Formations, Rally Points, Garrison | ✅ Completed |
| **2026-08-28** | **5 Large Maps** | Acropolis Bay, Corinthian Isthmus, Atlas Valleys, Bactria, Battle for Tiber | ✅ Completed |
| **2026-08-28** | **Multi-Civ & Wildlife** | 6 Civilizations, Wildlife Fauna, Hunting, Spline Wall Fortifications | ✅ Completed |
| **2026-08-29** | **Combat & Immersion** | High Ground Elevation, Adaptive Battle Music, Siege Battering Rams | ✅ Completed |
