# B audit pass 12: diplomacy, victory, options and formation control, against 0 A.D. 0.28

Checkout: `ce0c9db2d` (origin/main, 2026-11-02; landed on 36a87e841). A fresh static pass over the four B areas after 57 new commits on main, including the owner's perf, camera, touch and battle-preset patches. Items in passes 3-11 and in REMAINING_GAPS were skipped.

Sources: simulation /mnt/project-files/d-civ-rosters/0ad-028-src/simulation/ (Diplomacy.js, Player.js, EndGameManager.js, Formation.js, UnitAI.js, helpers/Commands.js, helpers/Cheat.js, data/cheats/), gui /mnt/project-files/session-ui/0ad-028-gui/session/ and gamesetup/, and the cached 0.28 public tree under /tmp/claude-0/oad028/pub (gui/options/options.js, gamesettings/attributes/*.js, maps/scripts/).

## Fixed in this pass

Diplomacy
1. The Diplomacy dialog's Team column and the observers' team colours used the simulation's team (its lowest member's id, which a player leaves on breaking an alliance). 0.28 shows the Game Setup team (DiplomacyPlayerText.js:165-168, DiplomacyColors.js:105-111), which no stance change alters: teams were renumbered by member order, a lone Team 3 player showed "None", and a player who broke with a teammate changed number and colour. `display_colors::set_setup_teams` now keeps the setup teams at every match start (`session_setup.cpp`), and `team_text` and `display_colors::rgb` read them.
2. "/enemies" during a ceasefire reached the enemies. StartCeasefire makes every enemy neutral (CeasefireManager.js:74-80), so ChatAddressees.js's isEnemy test reaches no one. `chat_reaches` now treats enemies as neutral while the ceasefire lasts.
3. `cmd_set_diplomacy` changed stances toward a defeated or winning player; Diplomacy.js SetDiplomacyIndex (156-163) changes nothing unless both are active. It also notified only real changes; Commands.js "diplomacy" notifies every accepted command.
4. "how do you turn this on?" (createPlane.json) was missing and went out as a chat line. It now creates planes at the selected trainer (CHEAT_CREATE_UNITS extra 2).

Victory
5. Conquest Structures defeated a player who started with no structures (an army-only start such as White Cliffs of Dover's players 2-5, or a Nomad map's JSON nomad flag the sim never reads) on the first check, and a Nomad whose first foundation was lost played on. ConquestCommon.js's "Structure" list fills with what the player holds, foundations included, and fires only when it empties. `had_structure_mask_` now tracks it like `had_unit_mask_`.
6. Allied victory needed every pair of remaining players to be mutual allies. EndGameManager.js AlliedVictoryCheck (143-155) checks each against the lowest-numbered remaining player only. `settle()` and the Last Man Standing line now do the same.

Options and Game Setup
7. Answering No to the GUI-scale countdown (or letting it run out) left Save and Revert lit. options.js revertChange clears the changes flag when there were none before (ConfigDB_SetChanges); `RtsoUserConfig.set_changes` does that now.
8. Team Placement kept its pick across map changes; PlayerPlacement.onMapChange sets Random (PlayerPlacement.js:24-33).
9. Battle presets (owner's T47, not in 0.28): a preset pressed and left with Back played in the next match started from elsewhere (a replay, a saved game), a preset survived a change of map, and replays of a preset match lacked its armies and forts. Game Setup now clears a leftover preset when it opens, passes the preset only with the map it picked, and the replay records `battle_preset` and `battle_units` (kSetupKeys). The suspected multiplayer desync does not happen: GameStart rebuilds the map on every peer.

Formation control
10. The idle-unit keys and the hero panel (click and double-click) selected one member of a formation; addList adds the whole formation with gui.session.selectformationasone (selection.js:296-306). They now use `formation_mates`.
11. A walking formation absorbed a twin that was fighting (FORDER_MEMBER), pulling its fighters off. Formation.js:949-951 merges only controllers in the world; MEMBER.enter takes a fighting one out.
12. A formation on patrol waited 3 s at each end; CHECKINGWAYPOINT's StartTimer(0, 1000) looks at 0, 1 and 2 s and leaves at 2 s.
13. After a fight on a lone patrol leg, 0.28 re-anchors the patrol start where the fight ended (PATROL.leave drops patrolStartPosOrder, PATROL.enter sets it again); RTS0 went back to the original start.
14. Idle re-form checks came 0-2 s after entering IDLE; IDLE.enter's StartTimer(100, 2000) checks after 0.1 s.

## Left open (REMAINING_GAPS)

- A "Removed" Game Setup slot is dropped (later players move up). 0.28 keeps the slot, places the player and defeats it at the start, so its map entities become Gaia's (PlayerRemoved.js, InitGame.js:49-53). Medium; needs the setup slot model to carry removed players.
- Push-front on a whole formation drops the order it was carrying out (UnitAI PushOrderFront keeps it queued). Low; session.pushorderfront is unbound.
- Formation members walking to their slots answer attacks (violent stances while marching, any stance but passive when forming up); 0.28's FORMATIONMEMBER.WALKING ignores Attacked. Low.
- With Team Population or a rated game, a map change leaves Teams Locked ticked; 0.28's LockedTeams.onMapChange unticks it while the pop type keeps team population (a 0.28 quirk). Deliberate.
- An observer typing "/allies hi" sees "Unknown chat command"; 0.28 drops it silently. Nit.
- On touch devices (owner's 95de297ee) the 44 px formation and stance buttons wrap four to a row and overflow the panel. Device builds' area.
- Chat "attack" links jump to the spot; 0.28's focusAttack follows a unit target (setCameraFollow). The owner's d15bfe58e changed it deliberately ("camera control exclusively user-driven").
