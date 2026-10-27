# B audit pass 9: B's hotkeys, and Game Setup's victory and diplomacy settings, against 0 A.D. 0.28

Checkout: `28681f53b` "Merge pull request #1554 from abhisheksatyam123/claude/project-thread-uobx7j". This was a read-only static review: nothing was built or run.

Area:
- the hotkeys tied to B's controls: session.toggledefaultformation, the formation and stance hotkeys, session.gui.diplomacy.toggle, session.diplomacycolors, session.masstribute, the chat hotkeys, and session.kill, session.stop and session.backtowork where they touch formations. Their default keys, what they do in a match, and when they do nothing.
- Game Setup's VictoryConditions, Wonder Duration, Relic Count, Relic Duration, Hero Garrison (RegicideGarrison), Nomad, Ceasefire, Teams Locked, Last Man Standing, Explored Map, Revealed Map, Allied View and Cheats: labels, tooltips, defaults, allowed values, order, and what each one enables or disables.

Items already listed in passes 3-8, in REMAINING_GAPS (G38, G47, G53, G54, U6, U10, SK2, CA15 and the B row) and in the Session UI audits were skipped.

Sources:
- The shared folder (/mnt/project-files/session-ui/ and /mnt/project-files/d-civ-rosters/) returned I/O errors on every retry during this pass, so none of it was read.
- The 0.28 GUI, gamesettings and simulation files cited below come from the official 0.28.0 release archive cached at /tmp/claude-0/oad028/0ad-0.28.0/binaries/data/mods/public/public.zip. Paths are relative to that mod root.
- That archive has no config/default.cfg. The default keys were therefore not re-checked against 0.28: see the note under "Checked and matching".

## Differences

1. **Stopping part of a formation halts the whole formation, and the stopped units stay in it.**
   - 0.28: session.stop calls stopUnits on the selection (gui/session/hotkeys/misc.xml:43-45; selection_panels_helpers.js:403-410). The Abort button does the same (unit_actions.js:1567-1571). Commands.js "stop" (:484-489) goes through GetFormationUnitAIs:
     - A lone selected unit is first removed from its formation (Commands.js:1545-1553), so it stops on its own and the formation carries on.
     - A part of a formation gets a new controller of the same template, or is ordered one by one (:1560-1588, :1633-1666). Only the whole formation stops its own controller (:1569-1578).
   - RTS0: `cmd_stop` calls `formation_stop` (src/simulation/simulation.cpp:6583). That idles every formation holding any of the ids (:7169-7183). The units are never taken out of the formation (no `formation_remove_units`). One member picked with the idle-unit keys, or with selection.singleselection, and stopped with H halts its whole formation, and the member stays in the formation.
   - Low-medium. Fix: in `cmd_stop`, call `formation_remove_units` for a single id, as `cmd_move` does at :6447-6450. For several ids, use `formation_controllers_for` (:7294), as the walk does, instead of `formation_stop`.
2. **Back to work leaves the unit in its formation, and on its turret.**
   - 0.28: UnitAI.BackToWork (simulation/components/UnitAI.js:4222-4248):
     - It leaves a turret first and fails if it cannot (:4230-4232).
     - It removes the unit from its formation (:4239-4244). Formation.RemoveMembers then re-forms the rest or disbands them.
     - The hotkey (misc.xml:47-49) and the button both call backToWork (selection_panels_helpers.js:513-523).
   - RTS0: `order_back_to_work` (src/simulation/simulation.cpp:20433-20470) hands the kept orders to `apply_order` and does neither.
     - Gatherers moved in the default formation and sent back with Y stay formation members. They are selected, walked and regrouped with it.
     - A unit on a wall or tower turret loses its work orders (`work_orders_.erase`) and stays up there. The turret tick then drops any non-attack order of an immobile turret unit (:9180-9187).
   - Low-medium. Fix: in `order_back_to_work`, before `apply_order`:
     - call `leave_turret` for a turret unit, and fail with the notice if it can't
     - call `formation_remove_units(&unit_ids[k], 1)` once the unit has work orders
3. **Wonder Duration, Relic Count and Relic Duration start at 20, 2 and 20. In 0.28 they start at 0, 1 and 0.**
   - 0.28:
     - Wonder.init sets duration 0 (gamesettings/attributes/Wonder.js:6). Relic.init sets count 0 and duration 0, and `_set` raises the count to at least 1 (Relic.js:6-7, :41-46). Nothing else sets them for a random or skirmish map: onMapChange only acts on scenarios.
     - WonderDuration.js:53 declares DefaultValue = 20, but GameSettingControl.js and GameSettingControlSlider.js never read DefaultValue. Only Checkboxes/LockedTeams.js:15 reads its own.
     - A fresh 0.28 setup, or one whose persisted file has no such value, therefore shows "Immediate Victory." for both durations and "1 relic". The same three files hold the same code in two other cached 0.28 copies (/tmp/claude-0/gs028, /tmp/claude-0/oadx).
   - RTS0: scenes/menu/gamesetup.gd:1950-1951 and :1957 select 20, 20 and 2. :2207-2209 fall back to them, and scenes/menu/match_settings.gd:43-45 default to them. The comment at gamesetup.gd:166-167 says the 0.28 default is 20 and 2.
   - Low. 0.28's own default looks unintended (the dead DefaultValue = 20), so this is a judgement call. Fix: select 0, 1 and 0 (VICTORY_TIMES.find(0), RELIC_COUNTS.find(1)) in those three places. Or keep 20, 20 and 2, and correct the comment to say that RTS0 departs from 0.28 on purpose.
4. **Explored Map and Revealed Map are independent.**
   - 0.28: MapExploration.setRevealed also ticks Explored, and setExplored(false) also unticks Revealed (gamesettings/attributes/MapExploration.js:40-54). Both checkboxes call these setters (Checkboxes/ExploredMap.js:17-20, RevealedMap.js:17-20). A scenario's RevealMap applies the same rule (MapExploration.js:30-38). The game description then lists both (gui/common/gamedescription.js:396-404).
   - RTS0: nothing links %ExploreMap and %RevealMap (scenes/menu/gamesetup.gd:1924 connects only LockTeams, and :2354-2355 read them separately). The scenario path at :1158-1160 copies RevealMap only. Ticking Revealed Map leaves Explored unticked, and the description reads "Explored Map: No, Revealed Map: Yes" (scenes/session/game_description.gd:175-176). This also affects the 18 scenarios in data/map_types.json with RevealMap, such as the sandboxes and combat_demo.
   - Low. Fix: next to gamesetup.gd:1924, connect RevealMap.toggled (on also presses ExploreMap) and ExploreMap.toggled (off also releases RevealMap). In `_apply_map_locks` (:1160), set ExploreMap when the scenario reveals the map.
5. **Locking teams leaves Last Man Standing ticked.**
   - 0.28: LastManStanding watches lockedTeams.enabled, and setEnabled(enabled && available) clears it once teams lock (gamesettings/attributes/LastManStanding.js:7, :28-37). Unlocking does not bring it back. The checkbox shows the cleared state (Checkboxes/LastManStanding.js:15-21).
   - RTS0: `_render_mode_checks` only disables %LastManStanding (scenes/menu/gamesetup.gd:2061). The box stays ticked while greyed out, and becomes active again when teams unlock. `_settings` only drops it from what is sent (:2350).
   - Low. Fix: at :2061, `if %LockTeams.button_pressed: %LastManStanding.set_pressed_no_signal(false)`.
6. **Unticking Regicide keeps Hero Garrison ticked.**
   - 0.28: RegicideGarrison watches the active conditions, and setEnabled(enabled && available) clears it when Regicide goes (gamesettings/attributes/RegicideGarrison.js:6, :28-37). Ticking Regicide again shows Hero Garrison unticked.
   - RTS0: `_render_mode_checks` only hides %RegicideGarrison (scenes/menu/gamesetup.gd:2063). It comes back ticked. `_settings` sends it only with Regicide (:2374).
   - Low. Fix: at :2063, release it when Regicide isn't checked.
7. **Changing the population cap type leaves Teams Locked as it was.**
   - 0.28: LockedTeams watches population.capType (gamesettings/attributes/LockedTeams.js:8). On every change it sets enabled = (capType == "team") (:34-38). Observable fires on any changed value (gui/common/Observable.js:33-35). Leaving Team Population, or switching between Player and World Population, therefore unlocks the teams, including the multiplayer default of locked.
   - RTS0: `_update_lock_teams_for_pop` presses LockTeams only for Team Population. Otherwise it only re-enables the box (scenes/menu/gamesetup.gd:804-811). `_set_pop_type` (:751) never releases it.
   - Low. Fix: in `_set_pop_type` (:751), when the type changes, set %LockTeams to `type == POP_TYPE_TEAM`, then call `_update_lock_teams_for_pop`.
8. **A multiplayer host's Cheats choice is not persisted.**
   - 0.28: the controller saves every setting, CheatsEnabled included (GameSettingsController.js:316-320 via getSettings), to config/matchsettings.mp.json (Persistence/PersistentMatchSettings.js:8-10). It restores them on load (GameSettingsController.js:79-97) before the controls' onLoad handlers run (SetupWindow.js:40-41, :66-67). Cheats.onLoad only forces cheats on in single player (Checkboxes/Cheats.js:14-19). With "Persist match settings" on, a host who enabled cheats gets them enabled again next time.
   - RTS0: "cheats" is not in PERSISTED_KEYS (scenes/menu/gamesetup.gd:2086-2090), and the host's box is set off unless it is a saved game (:2626).
   - Low. Fix: persist "cheats" from the multiplayer file only, and at :2626 restore it for a host (still never for a client, and off for a rated game).

## Checked and matching 0.28

- **Hotkey names:**
  - 0.28 has no formation hotkeys other than session.toggledefaultformation, no stance hotkeys, and no session.chat, session.teamchat, session.observerchat or session.privatechat. The chat hotkeys are the [hotkey] keys chat, teamchat and privatechat (Chat.js:45-47; gui/hotkeys/spec/ingamegui.json:22-33). RTS0 reads the same names (src/presentation/session_input.cpp:162-171).
  - RTS0's [hotkey.rts0.stance] keys are its own and unbound by default (hotkey_defaults.h), so a default setup behaves as 0.28.
- **Default keys:** hotkey_defaults.h says it holds 0.28's default.cfg [hotkey.*] verbatim: chat Return, teamchat T, privatechat L, session.kill Delete/Backspace, session.stop H, session.backtowork Y, session.masstribute Shift, session.toggledefaultformation unbound, session.diplomacycolors Alt+X, session.gui.diplomacy.toggle Ctrl+H. These agree with the default.cfg cached at /tmp/claude-0/dc.cfg. That file's version is not recorded, and the shared 0.28 config could not be read, so they are not confirmed against 0.28 in this pass. Pass 3 checked the diplomacy ones.
- **session.toggledefaultformation:** None and the last default alternate, the toggle starts from the saved default, and the choice is saved (AutoFormation.js:95-122; main_node.cpp:1243-1255).
- **session.gui.diplomacy.toggle:** toggle closes the other dialogs, and opens nothing while no player is viewed (DiplomacyDialog.js:52-80; main_node.cpp:4518-4521 with the dialog's own check). The top-panel tooltip is "%(hotkey)s Diplomacy" (DiplomacyDialogButton.js:16-21; session_lifecycle.cpp:1646).
- **session.diplomacycolors:** off at the start of every match and not saved. A press redraws the models, minimap, dialog and player-view lists (DiplomacyColors.js:11, :67-71; main_node.cpp:616-623). It works for observers too, as the minimap button carries the hotkey (MiniMap.xml:50).
- **Chat hotkeys:**
  - Each one opens the chat afresh after closing the dialogs, and does nothing while disconnected (Chat.js:58-68; session_lifecycle.cpp:1605-1611).
  - chat selects Everyone. teamchat selects Allies, or Observers for an observer, a defeated player or a winner (g_IsObserver; chat_window.h:271 via observing_seat). privatechat selects the last private partner.
  - The three tooltip lines (ChatInput.js:40-49; session_lifecycle.cpp:1626-1632).
- **session.kill:** "delete" on the selection, with DeleteSelectionConfirmation unless session.noconfirmation is held. It does nothing to formations beyond the deaths themselves (misc.xml:27-29; unit_actions.js:1542-1545; session_commands.cpp:396-438).
- **session.stop and session.backtowork:**
  - both send the whole selection, with no availability check (misc.xml:43-49)
  - back to work keeps only units with work orders
  - with gui.session.formationwalkonly, neither sends the null formation (main_node.cpp:3833-3844)
  - stopping a whole formation stops its controller
  - items 1 and 2 aside
- **Victory conditions:**
  - the seven titles and descriptions (simulation/data/settings/victory_conditions/*.json)
  - the order: GUIOrder, then title (gui/common/settings.js:234): Conquest, Conquest Structures, Conquest Units, Conquest Civic Centers, Capture the Relic, Regicide, Wonder
  - Conquest as the only default
  - DisabledWhenChecked: Conquest excludes the other three conquests; Units excludes Conquest; Structures excludes Conquest and Civic Centers; Civic Centers excludes Conquest and Structures
  - every box disabled on a scenario, whose list replaces the checked ones
  - nothing checked means an endless game
- **Wonder Duration, Relic Count and Relic Duration:**
  - titles and tooltips
  - 0-60 minutes, and 1 to 15 relics (15 civ files, as g_CivData)
  - "Immediate Victory." at 0
  - shown only while their condition is checked, and disabled on a scenario
  - placed after the conditions as Relic Count, Relic Duration, Hero Garrison, Wonder Duration
  - item 3 aside
- **Hero Garrison:** "Hero Garrison" and "Toggle whether heroes can be garrisoned.", hidden on a scenario or without Regicide, sent only with Regicide. Item 6 aside.
- **Nomad:** title and tooltip, off by default, shown only on random maps, sent only for them.
- **Ceasefire:** "Ceasefire", "Set time where no attacks are possible.", 0-45 minutes, "No ceasefire" by default, disabled on a scenario and taken from its Ceasefire.
- **Teams Locked:**
  - "Teams Locked" and "Toggle locked teams."
  - locked by default in multiplayer and open in single player, whatever was persisted
  - forced on and disabled with Team Population or a rated game, and disabled on a scenario
  - item 7 aside
- **Last Man Standing:** title and tooltip, always shown, disabled on a scenario or with locked teams, not sent with locked teams. Item 5 aside.
- **Explored Map, Revealed Map and Allied View:** titles and tooltips, off by default, disabled on a scenario and taken from it. Item 4 aside.
- **Cheats:**
  - "Cheats" and "Toggle the usability of cheats."
  - always on and disabled in single player
  - off by default for a multiplayer host, and disabled once the game is rated
  - "Cheats enabled." in the setting warning
  - item 8 aside
- **Game Type tab order:** the conditions, Relic Count, Relic Duration, Hero Garrison, Wonder Duration, Game Speed, Ceasefire, Teams Locked, Last Man Standing, Rated Game (GameSettingsLayout.js:39-52). RTS0's Waves row is its own addition.

## Status

Fixed on main ca22e1c09:
1. Stop: the player's Stop (session button, hotkey and the lockstep command) goes through `order_stop`, which splits the units as a walk does (`formation_controllers_for`). A lone unit leaves its formation, a part of a formation gets its own controller, and only the stopped controllers idle. The sim's own internal stops keep `cmd_stop`. test_formations 17.
2. Back to work: `order_back_to_work` takes a unit off its turret first (failing with the notice if it can't step down), then removes it from its formation before its work orders resume. test_formations 17.
3. Kept on purpose: Wonder Duration, Relic Duration and Relic Count open at 20 min, 20 min and 2. The comment at gamesetup.gd's slider constants now says so, and REMAINING_GAPS G52 lists it as deliberate.
4. Ticking Revealed Map ticks Explored Map, and unticking Explored Map unticks Revealed Map. A scenario's RevealMap explores the map too. e2e_setup_map_type 3e.
5. Locking teams clears Last Man Standing, and unlocking leaves it cleared. e2e_setup_map_type 3d.
6. Unticking Regicide clears Hero Garrison. e2e_setup_map_type 3d.
7. Picking another population cap type sets Locked Teams to (type is Team Population). e2e_setup_map_type 3e.
8. A multiplayer host's Cheats choice is saved in matchsettings.mp.json and restored for a new game's host. It is never restored for a client, and is off in a rated game.

G52 is closed.
