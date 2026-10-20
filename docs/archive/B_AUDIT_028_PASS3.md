# 0.28 audit, pass 3: diplomacy dialog, options page, rank/XP UI

Checkout: `a3a765bbd` "Chat: 0.28's centred chat dialog ... closes G23" (2026-10-15). Read-only review; nothing built or run.

## Confirmed differences

1. **Diplomacy colours ignore the ceasefire.** In 0.28 the ceasefire sets every enemy to Neutral (CeasefireManager.js:76-80), so DiplomacyColors.js:116-119 draws enemies yellow while it lasts. RTS0 `display_colors::rgb` (src/presentation/display_colors.h:73) reads raw `get_diplomacy` and draws them red. The diplomacy dialog itself uses `effective_stance` (diplomacy_dialog.h:95), so the two disagree. **Medium.** Fix: use `effective_stance` (enemy becomes neutral while `ceasefire_active()`) in `display_colors::rgb`.
2. **A tribute you cannot afford fails silently.** 0.28 always posts the command, and Player.js:679 (via SubtractResourcesOrNotify, :286-316) shows "Insufficient resources - %(amount)s %(type)s". RTS0 drops it in lockstep (diplomacy_dialog.h:571), and offline `cmd_tribute` returns false with no notice (simulation.cpp:3822). **Medium.** Fix: post it anyway and have `cmd_tribute` raise the insufficient-resources notice.
3. **Options Reset erases too much.** 0.28 setDefaults removes only the options.json keys (options.js:372-376). RTS0 `reset_all` (user_config.cpp:241-245) clears every known key, including lobby.login/password, mod.enabledmods, gui.splashscreen.version, lobby.buddies and the multiplayer ports. **Medium.** Fix: erase only the keys listed in options.gd SCHEMA.
4. **The scroll batch slider cannot reach 1 or 30.** options.json:813-815 gives min 0.1, max 30, and the 0.28 slider is continuous. options.gd:405 uses a step of 1.0 whenever max > 2, so the only values are 0.1, 1.1 … 29.1, and the default of 1 snaps to 1.1. **Medium.** Fix: a step of 0.1 for scrollbatchratio (or a step taken from the minimum).
5. **No colour picker on colour options.** options.js:82-91 opens page_colormixer.xml from a button. options.gd:444-455 has only a text field and a swatch. **Low.** Fix: add a button that opens a ColorPicker popup and writes back "R G B".
6. **Graphics (advanced) is missing options.** 0.28 lists rendererbackend, gpuskinning and max_actor_quality, and repeats fog, postproc and shadows here (options.json:189-291). options.gd:53-76 has none of these, so the shadow and postproc dependencies point at toggles on another tab. **Low.** Fix: repeat fog, postproc and shadows on this tab, and add Model quality if it can be mapped to an LOD.
7. **Some list items and options are missing.** Upscale technique has no "pixelated" (options.json:83; options.gd:38, although ITEM_TIPS:183 has its text). Antialiasing has no MSAA 16× (options.json:230; options.gd:55). lobby.tls is absent (options.json:536). **Low.** Fix: add them, or note them as unsupported.
8. **VSync tooltip loses "REQUIRES GAME RESTART".** options.json:108 vs options.gd:42. This is correct only if `user_settings.gd:501` applies VSync live, which it does. **Low (intentional?).** Fix: keep as is and document the reason.
9. **Invalid values: no warning flow.** 0.28 stores what was typed, marks it invalid and asks "Some setting values are invalid! Are you sure you want to save them?" (options.js:95-127, 410-435). RTS0 silently keeps the old colour value (options.gd:495-501) and clamps numbers in a SpinBox (:415-423). **Low.** Fix: optional; document it as a deliberate simplification.
10. **Dropdown item tooltips appear differently.** 0.28 shows the hovered item's tooltip, else the option's own (options.js:154-157). RTS0 adds the selected item's tip to a description label at the bottom (options.gd:479-483). **Low.** Fix: set a tooltip for each popup item.
11. **Extra and reordered options.** RTS0 adds a "locale" dropdown to General (options.gd:27; 0.28 has a separate Language page). The Display tab order differs: 0.28 starts with showdetailedtooltips (options.json:653), RTS0 puts it 9th. lobby.history has max 1000 in RTS0 (options.gd:98) but no max in 0.28 (:542-543). **Low.** Fix: reorder Display to match options.json and drop the lobby.history max.
12. **Spy requirement tooltip text.** 0.28 shows getRequirementsTooltip, "Requires %(technology)s" in objectionFont (zx/gui/common/tooltips.js:974; SpyRequestButton.js:206). RTS0 shows "Requires Espionage (research it at the market)." (diplomacy_dialog.h:394). **Low.** Fix: use the 0.28 string and the tech name.
13. **Ceasefire counter rounds up.** timeToString truncates; RTS0 uses `std::ceil` (diplomacy_dialog.h:431), so it reads one second high. **Low.** Fix: use `floor`.

## Already matches 0.28

- **Diplomacy:** close and reopen behaviour (closeOpenDialogs, g_ViewedPlayer). Row tint uses the diplomacy colour at alpha 32. Name, civ, team ("None"), Theirs, and outcome icons with their tooltips. Stance buttons: "x" caption, hidden during ceasefire or locked teams, disabled without controlsPlayer. Tribute: 100 default, Shift raises to the next 500, release sends, amount resets, exact tooltip string. Spy button: cost × multiplier, needed resources, failure cost, pending state, follow-camera. Attack request needs a mutual ally and no ceasefire. Ceasefire caption. Colours button: sprite on/off and tooltip with the hotkey. Hotkeys: diplomacycolors Alt+X, diplomacy.toggle Ctrl+H, masstribute Shift. Diplomacy and tribute chat lines. There is no diplomacy cheat in 0.28 (Cheat.js), so nothing to port.
- **Options:** every other option's label, tooltip, range and list; the dependency logic, with labels indented 25 px; the gui.scale 5 s timed confirmation; slider, number and colour tooltip suffixes; Revert and Save enabled only when there are changes; the close and reset confirmation texts. Defaults match the default.cfg on hand except windowed=true, which is intentional. No 0.28 default.cfg exists locally, so newer keys could not be checked.
- **Rank/XP:** the rankIcon "%(rank)s Rank" tooltip plus the rank tech tooltip; the experience bar fill and the "Experience: cur / req" (or cur only) tooltip with floor/ceil; rankabovestatusbar and experiencestatusbar overlays; formationwalkonly null-formation order set (matches unit_actions.js getNull users); selectformationasone with the singleselection override.

## Status (2026-10-15)

- Fixed in the pass-3 PR: 1 (diplomacy colours follow the ceasefire), 2 (an unaffordable tribute is sent and answers "Insufficient resources - ..."), 3 (Reset erases only the page's options), 4 (scroll batch slider in tenths), 12 ("Requires Espionage"), 13 (ceasefire counter truncates).
- Left open as REMAINING_GAPS G36 (low): 5, 6, 7, 9, 10, 11.
- 8 kept: VSync applies live in RTS0, so it needs no restart.

## Status (2026-10-20)

- Fixed in the G36 PR: 5 (the colour button opens a port of page_colormixer, scenes/common/colormixer.gd), 9 (colours store what was typed; Save shows the tab and asks), 10 (each list item carries its own tooltip), 11 (Display follows options.json; lobby.history has no maximum).
- Still open in G36: 6 and 7, which need renderer modes Godot 4.3 lacks.
