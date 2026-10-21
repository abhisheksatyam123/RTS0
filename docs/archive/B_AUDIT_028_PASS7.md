# B audit pass 7: promotion and XP, the rank UI, chat addressees, resign and quit, against 0 A.D. 0.28

Checkout: `9f11176dd` "Merge pull request #1498 from abhisheksatyam123/claude/project-thread-uobx7j". This was a read-only static review: nothing was built or run.

Area:
- the promotion simulation: Promotion.js, Transform.js ChangeEntityTemplate, Health.js and helpers/Attack.js damage XP, Heal.js healer XP, Loot.js, the xp_trickle aura, and the RequiredXp techs and auras
- the rank and XP UI: selection_details.js, selection.js EntityGroups, and the selection and garrison panels
- chat: everything under gui/session/chat/
- resign and quit: MenuButtons.js, message_box/*, session.js and PlayerViewControl.js

Items already listed in passes 3-6 and in REMAINING_GAPS (U19, S1, G36, the session chat rows) were skipped.

## Differences

1. **The selection panel puts every rank of a unit in one group.**
   - 0.28: selection.js:66-72 keys each group by template name, and Basic, Advanced and Elite are separate templates (`_b`, `_a`, `_e`). Three ranks of spearmen therefore show as three icons, which can be selected or deselected one at a time. A unit that promotes moves to the next icon. The garrison panel groups the same way (selection_panels.js:331-337).
   - RTS0: `RtsoSelectionGrid::group_key` (src/presentation/selection_grid.h:61) and `owner_key` (:69) key on the roster template only, so the ranks share one icon. `select_group` (src/presentation/main_node.cpp:2297) and the sort key (src/presentation/inspector_view.h:1652-1662) use the same key.
   - Medium-low. Fix: add `uview.rank` to the key in all three places. The sort should place the ranks in `_a` < `_b` < `_e` order, as 0.28's sorted template names do.
2. **Range auras that lower RequiredXp reach garrisoned units.**
   - 0.28: Promotion.js:24 reads RequiredXp through the ModifiersManager. Range auras apply through a RangeManager active query (Auras.js:284), and that query skips out-of-world entities. A garrisoned unit is moved out of the world (Garrisonable.js:100). A unit gaining xp in a barracks next to a centurion, a Germanic seer or Han Xin therefore still needs its full RequiredXp.
   - RTS0: `promotion_required_xp` (src/simulation/simulation.cpp:16237) queries `AURA_REQUIRED_XP` at the unit's stored position, garrisoned or not. `tick_promotion` (:16365-16367) then promotes trickling units at 75% or 80% of their RequiredXp. The no-loot aura check (`loot_denied_unit`) and capture attack already exclude garrisoned units.
   - Low. Fix: skip the range query when `action_state == ACTION_GARRISONED`.
3. **A private message to a defeated player is never shown.**
   - 0.28: Chat.js:104-116 sends a /msg to [sender, addressee GUID]. The receiver shows it when ChatAddressees.js:138-139 passes (the sender is not an observer, or the receiver is) and ChatMessageFormatPlayer.js:282-283 matches the GUID. A defeated player listed in the dropdown therefore gets the line.
   - RTS0: `chat_private_targets` (src/presentation/main_node.cpp:4196) tags any finished player as `[Observer-<client>]`. `chat_tag_reaches` (src/network/chat_routing.h:63) passes that tag only to a seatless client, and test_net_host.cpp:316 asserts this. Both the host relay and the receiver therefore drop the line. The `[Player-N]` path, which test_net_host.cpp:313 shows reaching a defeated player, is never used for them.
   - Medium-low. Fix: tag `[Observer-C]` only when `e.player >= MAX_PLAYERS`, and `[Player-N]` for a seat whose game is over.
4. **"Players chat" still counts lines from defeated players.**
   - 0.28: ChatHistory.js:98-100 requires `senderID > 0 && !isPlayerObserver(senderID)`. A defeated or victorious sender is an observer (session.js:456-460).
   - RTS0: `chat_sender_is_player` (src/presentation/main_node.h:633) tests only `chat_player_of(guid) < MAX_PLAYERS`. The local echo already uses `!observer` (chat_window.h:407).
   - Low. Fix: `&& !player_finished(chat_player_of(guid))`.
5. **A typed /msg rebinds the private-chat hotkey.**
   - 0.28: Chat.js:85-86 rebinds `privatechat` only when the To: dropdown's command is a /msg. A typed "/msg Bob hi" sent with To: Everyone passes command "" and leaves the hotkey alone.
   - RTS0: chat_window.h:404 sets `last_private_` for any private line, typed ones included.
   - Low. Fix: set it only when `selected_tag()` was already a private tag.
6. **Phase chat lines name only the generic phase.**
   - 0.28: ChatMessageFormatSimulation.js:119 uses `getEntityNames(GetTechnologyData(msg.phaseName, civ))`, which gives "Alice has reached the Kōmopolis (Town Phase)." under the default howtoshownames=0. phase_town_generic.json, phase_town_athen.json and phase_city_pers.json have specific names.
   - RTS0: src/presentation/session_notices.cpp:140 uses `tech_info(n.tech).name` ("Town Phase"), although `tech_specific_name` holds the specific names.
   - Low. Fix: pass the name through `rtso::entity_names(generic, tech_specific_name(tech, player_civ(n.player)))`.
7. **An observer's view stays on a player after that player is defeated.**
   - 0.28: PlayerViewControl.js:66-70 switches the view to "Observer" when the viewed player is defeated, and keeps it when they win. This covers observers, replays and earlier-defeated players watching someone else.
   - RTS0: `update_observer_view_picker` (src/presentation/main_node.cpp:1450-1483) picks a view only once, the first time the client spectates. The replay picker (`set_replay_view`, src/presentation/match_archive.cpp:726) has no defeat hook either. An observer watching a player who is defeated keeps that player's view and HUD.
   - Low-medium. Fix: when the viewed player becomes defeated, call `set_observer_view(-1)` (or `set_replay_view(-1)` in a replay).

## Checked and matching 0.28

- **Promotion.js IncreaseXp:**
  - leftover xp carries over, and several ranks can come at once
  - the first step uses the unit's RequiredXp with auras; later steps use the template's
  - a dead unit doesn't promote
  - RequiredXp drops promote at once (OnValueModification, checked every tick)
  - the promote cheat brings xp to RequiredXp
- **Damage XP:** Health.js:196-197 and helpers/Attack.js:338-344.
  - The victim's Loot/xp times the health actually lost, over max health.
  - Splash damage gives xp; status effects give none; invulnerable targets give none.
  - Han Ministry 200 xp, the only structure with any; Gaia animals give xp, owned livestock none.
  - Pericles' no-loot aura gives 0.
- **Loot/xp:** x1.2 per rank (unit_advanced.json and unit_elite.json), floored as in Loot.js:61.
- **Heal.js:226-231:** the health healed over max health, times the target's Loot/xp.
- **xp_trickle:** the barracks, stable and elephant stable families (Han academy, Ptolemaic and Egyptian mercenary camps), 1 xp a second for any garrisoned unit that has a Promotion, allies included.
- **RequiredXp effects:**
  - techs: Art of War, Hoplite Tradition, Marian Reforms, Silver Shields, Wagon Trains, soldier_ranged_experience, upgrade_rank_advanced_mercenary
  - range and global auras: centurion, Germanic seer, Han Xin, Gallic catafalque
- **What a promoted unit keeps:** RTS0 keeps the entity id, which covers everything 0.28 copies over.
  - ChangeEntityTemplate (Transform.js): health fraction, stance, orders, held position, guard, status effects, carried resources, garrison and turret
  - Formation.js:1004-1022 and Garrisonable.js:162-171
  - selection and control groups (selection.js:274-290, 569-585)
  - StatisticsTracker counts no promotion.
- **XP UI:** the experience bar, shown only with a Promotion (GuiInterface.js:556-561), with its fill and its "Experience: cur / req" or "cur" tooltip (selection_details.js:183-200).
- **Chat addressees:**
  - dropdown order and the observer rules (ChatAddressees.js:41-86)
  - Allies needs a mutual ally; Enemies goes by the sender's stance
  - Observers; observer-to-player private messages refused
  - the "[OFFLINE]" entry, the longest-name /msg match, typed commands only with To: Everyone
- **Chat formatting:**
  - "(context) <user>" and the /me forms; text escaped
  - the nick chime
  - the nine history filters and "[HH:mm]" timestamps
  - the 20-line, 30 s overlay that stays up while the window is open
  - closePage empties the input; the History tickbox saves chat.session.extended
  - openPage closes the other dialogs and refuses while disconnected
- **Chat notifications:** the diplomacy, tribute (LockTeams and mutual-ally rule), barter (observers only) and playerstate lines.
- **Resign and quit:**
  - Resign disabled for observers; ResignConfirmation
  - QuitConfirmationMenu's three cases with their captions
  - ReturnQuestion
  - QuitConfirmationDefeat:
    - VICTORIOUS! or DEFEATED!, with "Do you want to quit?"
    - the host with humans still playing gets no buttons
    - not asked again on rejoin
  - Stay, Quit and View Summary, Quit; endGame's destinations
  - single-player pause while a box is up
  - Commands.js "resign"; defeated entities go to Gaia; the winner's map is revealed
  - after a defeat the player's own view goes to Observer; a winner keeps their view

## Status (2026-10-21)

All seven are fixed in the pass-7 PR:
- test_promotion 5b checks that a garrisoned soldier gets no range RequiredXp aura.
- e2e_observer_view checks the phase line's civ name, and that the view switches to Observer when the viewed player loses (tests use the new debug_defeat_player hook).
- The chat changes and the per-rank selection groups have no automated test (the chat ones need a networked match). e2e_session_dialogs and e2e_selection_details still pass.
