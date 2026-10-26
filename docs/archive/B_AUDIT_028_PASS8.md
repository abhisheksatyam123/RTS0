# B audit pass 8: the diplomacy dialog, formation and stance buttons, Options, and the victory screens, against 0 A.D. 0.28

Checkout: `e0347ae89` "Merge pull request #1525 from abhisheksatyam123/claude/project-thread-uobx7j". This was a read-only static review: nothing was built or run.

Area:
- the diplomacy dialog: gui/session/diplomacy/ (DiplomacyDialog*.js, playercontrols/*.js, DiplomacyDialog.xml)
- the Formation and Stance panels: selection_panels.js, selection_panels_helpers.js, AutoFormation.js, GuiInterface.js, Commands.js CanMoveEntsIntoFormation, template_player.xml and special/players/*.xml, special/formations/*.xml
- the Options page: options.js, options.json, options.xml and default.cfg
- the victory and defeat screens: summary.js initGUILabels, messages.js g_PlayerStateMessages, gamedescription.js getGameDescription, and match_settings/MatchSettingsDialog.js. 0.28 has no gui/session/objectives/: the dialog RTS0 once called "Objectives" is 0.28's Match Settings, which earlier passes already cover.

Items already listed in passes 3-7, in REMAINING_GAPS (G36, the S, T, U, SP and RP rows) and in the Session UI audits were skipped.

## Differences

1. **The formation buttons come in RTS0's enum order, not the player's list order.**
   - 0.28: selection_panels.js:269 keeps the order of Player.js GetFormations. That is template_player.xml:83-97 (None, Box, Forced March, Close Order, Column Open, Open Order, Flank, Battle Line, Skirmish, Wedge, then the three fleet formations), followed by the civ's own from special/players/<civ>.xml (Phalanx for athen, and so on; Player.js:64 splits the merged token list). An Athenian spearman group shows None, Box, Forced March, Close Order, Column Open, Open Order, Flank, Battle Line, Phalanx.
   - RTS0: src/presentation/command_queue_view.h:924 walks `f = 0 .. FORMATION_COUNT` in UnitFormation order (sim_types.h:961-979), which keeps old save ids. The same group shows None, Close Order, Box, Forced March, Phalanx, Open Order, Column Open, Flank, Battle Line.
   - Low. Fix: walk a fixed display-order table (template_player's order, then syntagma, testudo, anti_cavalry and shield_wall as each civ file lists them) instead of the enum.
2. **Fleet Battle Line shows the Close Order icon.**
   - 0.28: special/formations/ships/line_staggered.xml:18 uses formations/battle_line.png, which GuiInterface.js:865 hands to selection_panels.js:312.
   - RTS0: `kFormationIcons[FORMATION_SHIPS_LINE]` is "ship_line" (src/presentation/command_queue_view.h:904-907). tools/extract_ui_assets.py:270-276 writes that file from line_closed.png, and assets/textures/ui/formations/ship_line.png matches line_closed.png pixel for pixel. formation_data.h already holds the right icon path for every formation.
   - Low. Fix: load `ft.icon` (formation_data.h) at command_queue_view.h:956 and drop the kFormationIcons table, or point ship_line at battle_line.png.
3. **The formation and stance tooltips lose 0.28's fonts.**
   - 0.28: the formation description is bodyFont and the disabled reason objectionFont, red and bold (selection_panels.js:292, :305). The stance description is bodyFont (selection_panels.js:978). Both panels use a bold tooltip style, so only the name stays bold.
   - RTS0: command_queue_view.h:937-945 and :207 join plain strings under the "session_bottom_bold" style (:178, :226). The whole tooltip is therefore bold, and "Requires at least 4 Soldiers or Siege engines." is not red. `rtso::body_font` and `rtso::objection_font` (l10n.h:96, :108) exist for this.
   - Low. Fix: wrap the description in `body_font` and the disabled line in `objection_font`, in both places.
4. **Toggling the default formation back on always gives Box.**
   - 0.28: AutoFormation.js:21-22 starts lastDefault from the saved gui.session.defaultformation, or Box when that is the null formation. With Close Order saved, session.toggledefaultformation goes Close Order, then None, then Close Order again.
   - RTS0: `last_default_formation_` starts at FORMATION_BOX (src/presentation/main_node.h:438) and only changes in `set_default_formation` (main_node.cpp:1229-1233). The same presses give Close Order, then None, then Box. Pass 4 checked the hotkey itself, not this starting value.
   - Low. Fix: when a session starts, set `last_default_formation_` from `RtsoCommandPanelView::default_formation()` if that is not FORMATION_NONE.
5. **The stance buttons show for selections that 0.28 gives none.**
   - 0.28: selection_panels.js:968 returns no stances when any selected entity is an Animal, so your own sheep or cattle in the selection hide the panel. It also lists `unitEntStates[0].unitAI.selectableStances`, which UnitAI.js:6180-6185 empties for a unit on a turret point (IsTurret, :3499-3504).
   - RTS0: command_queue_view.h:333-348 shows the five buttons whenever any selected id is a unit. Your own livestock (fauna, `fauna_selectable_by`) and turret units (`turret_holder_of`) are not checked. This is what remains of the Session UI selection audit's row 18.
   - Low. Fix: hide the bar when a selected id is the player's livestock, or when the first selected unit has `sim->turret_holder_of(id) != 0`.
6. **The tribute and spy tooltips are composed English, and lack the red parts.**
   - 0.28: TributeButton.js:81-91 translates its sentence, uses the resource name as written within a sentence, and colours the player's name (colorizePlayernameByID). SpyRequestButton.js:65-117 translates its lines. getRequirementsTooltip shows "Requires %(technology)s" in objectionFont (common/tooltips.js:974). When resources are short, the icon gets resourcesToAlphaMask's red tint (SpyRequestButton.js:104-105; selection_panels_helpers.js:39-46).
   - RTS0: src/presentation/diplomacy_dialog.h:378-380 builds "Tribute 100 food to Name. Shift-click to tribute 500." by concatenation, with the name uncoloured. :391, :395 and :408 are untranslated literals, and "Requires Espionage" is not in `objection_font`. :420 greys the spy icon in both disabled cases.
   - Low. Fix: use `sprintf0(tr0(...))` with the 0.28 msgids and a colourized name. Wrap the requirement in `objection_font`. Tint the icon red, alpha min(125, total/10 + 50), when the problem is missing resources.
7. **The diplomacy stance buttons carry tooltips that 0.28's lack.**
   - 0.28: diplomacyPlayerAlly/Neutral/Enemy[n] have no tooltip (DiplomacyDialog.xml:53-55; StanceButton.js sets none). Only the A, N and E headers have one (:26-37).
   - RTS0: diplomacy_dialog.h:206 gives each row's buttons "Ally", "Neutral" or "Enemy".
   - Low. Fix: drop the set_tooltip_text call. The headers (:166-169) keep theirs.
8. **The attack request button ignores a defeated mutual ally.**
   - 0.28: AttackRequestButton.js:19-24 hides the button only when the viewed player has no mutual ally. Diplomacy.js IsMutualAlly (:230-234) reads stances only, and Player.js SetState (:469-) leaves stances alone on defeat. With your only ally defeated, the button still shows, and Commands.js:780-795 posts "Attack against %(_player_)s requested." to /allies.
   - RTS0: diplomacy_dialog.h:423-426 counts an ally only when `!sim->is_defeated(q)`. The button hides once the last ally falls.
   - Low. Fix: drop the `!sim->is_defeated(q)` term, keeping `active`.

## Checked and matching 0.28

- **Diplomacy dialog:**
  - opens only with a player viewed, closes when the view leaves the players, closes the other dialogs first, refreshes while open
  - rows for every player, with controls hidden for yourself and for any player who is not active. A winner's rows end up the same way, since everyone else has won or lost.
  - name, civilization, the AI settings tooltip, the row tint at alpha 32, "Theirs" with the ceasefire's neutral, and the outcome icons with their tooltips
  - stance buttons: hidden during a ceasefire or with locked teams, an "x" on the current stance, disabled without controlsPlayer, Commands.js "diplomacy"
  - tribute: food, wood, stone and metal; 100 by default; Shift raises it to the next 500; Shift release sends it once it reaches 500; then back to 100; disabled without control; sent even when you can't pay
  - spy button:
    - hidden when spies are disabled, and for a mutual ally while you lack Cartography (Diplomacy.js HasSharedLos)
    - cost: 500 metal times the target's spyCostMultiplier, floored; what you're short; a failed bribe costs 0.25 of it
    - disabled while a bribe is pending; the camera follows the bribed unit
  - attack request: hidden during a ceasefire, for non-enemies and with no mutual ally, apart from item 8
  - "Remaining ceasefire time: %(time)s." with timeToString's truncated mm:ss or HH:mm:ss
  - the diplomacy colours button: its icons, and its tooltip with the hotkey
- **Formation panel:**
  - lists only the formations some selected unit can take, within the player's list
  - hidden while the Garrison panel shows
  - enabled when CanMoveEntsIntoFormation's RequiredMemberCount is met (the null formation always), and only with control of the units
  - every template's name, description, disabled reason and member count (formation_data.h against special/formations/*.xml)
  - the four default-formation lines and the default marker
  - right-click sets the default; gui.session.defaultformation defaults to Box
  - the formation a selected unit is in is lit (IsFormationSelected)
- **Stance panel:**
  - g_Stances order (violent, aggressive, defensive, passive, standground)
  - getStanceDisplayName and getStanceTooltip texts, and the stance icons
  - lit when a selected unit holds the stance (IsStanceSelected); disabled without control
- **Options:**
  - every label, tooltip, range and list item against options.json, apart from G36's leftovers and the VSync tooltip that pass 3 kept
  - every desktop default against 0.28's default.cfg, apart from windowed (kept on purpose) and lobby.tls (kept, with the reason in user_config.cpp:80). The low-end defaults are RTS0-only, used on Android and iOS or with -mobiledefaults.
  - the dependencies and their 25 px indentation
  - the gui.scale 5 s timed confirmation
  - the slider, number and colour tooltip suffixes
  - Revert and Save enabled only when there are changes
  - the Save, Reset and Close confirmations; Reset sets only this page's keys
  - every option key has a runtime reader
  - the batch-size reset that a new default triggers (input.js updateDefaultBatchSize)
- **Victory and defeat:**
  - summary.js initGUILabels: "Current Scores", "Scores at the end of the game.", the disconnected, left and abandoned lines, "You have won the battle!" and "You have been defeated…"
  - g_PlayerStateMessages in Match Settings ("You have won!", "You have been defeated!")
  - getGameDescription's victory lines: Endless Game; Wonder and Capture the Relic with their plural minutes; Relic Count; Hero Garrison or Exposed Heroes; Locked Teams or Diplomacy; Last Man Standing or Allied Victory; and the ceasefire line
  - The victory_conditions JSON files are not in the shared folder, so the order of those lines and their descriptions were not re-checked.

## Status (2026-10-21)

All eight are fixed in the pass-8 PR:
- Fleet formations use battle_line.png and column_closed's icon. The ship_line.png and ship_column.png extracts are now unused.
- The fleet formations' place in template_player.xml, and the civ files' order for civs with two extras, could not be re-read: the shared folder returned I/O errors. RTS0 keeps its enum order there.
- e2e_diplomacy_dialog checks the coloured tribute tooltip and the stance buttons without tooltips.
- e2e_formation_options checks that the toggle gives back a saved Close Order.
