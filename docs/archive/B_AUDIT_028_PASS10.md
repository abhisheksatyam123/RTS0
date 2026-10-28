# B audit pass 10: victory conditions in the match, and the Options page, against 0 A.D. 0.28

Checkout: `c0b327184` "Gaps: SW3 fixed (valley starts spread), SW5 0.28-correct (bare New RMS Test bases) (#1708)" on claude/project-thread-uobx7j. This was a read-only static review: nothing was built or run.

Area:
- Victory conditions as the match runs them: Conquest and its Structures, Units and Civic Centers variants (ConquestCommon.js and its queries), Wonder, Capture the Relic, Regicide, allied victory and Last Man Standing, resignation, the countdown notifications and the won/defeated chat lines, the VICTORIOUS!/DEFEATED! questions, and what happens to a player after defeat.
- The Options page (scenes/menu/options.gd, src/presentation/user_config.cpp, scenes/menu/user_settings.gd) against gui/options/options.json, options.js and options.xml: every option's key, label, tooltip, type, list, range, dependencies and default, and Reset, Revert, Save and Close.

Items already listed in passes 3-9 and in REMAINING_GAPS (G36, G47, G52, G53, G66, the B row, D9, D10, Q6, RP6, BA1, TU5) were skipped. That includes the deliberate choices: Wonder Duration, Relic Duration and Relic Count opening at 20, 20 and 2; allied victory needing a fight first; the Gaia relic counter; and the Godot 4.3 renderer limits (no Renderer backend, GPU skinning, Model quality, Pixelated or MSAA 16x).

Sources:
- /mnt/project-files/session-ui/0ad-028-gui/options/ does not exist in the shared folder, so the 0.28 GUI, simulation, trigger-script and template files cited below come from the official 0.28.0 release archive at /tmp/claude-0/oad028/0ad-0.28.0/binaries/data/mods/public/public.zip. Paths are relative to that mod root.
- That archive has no config/default.cfg. The Options defaults were compared with the default.cfg cached at /tmp/claude-0/dc.cfg, whose version is not recorded (as in pass 9), so they count as inferred.

## Differences

1. **The Options page only sees changes made since it opened, so Revert, Save and the Close question miss earlier unsaved changes.**
   - 0.28: Revert and Save are enabled while `Engine.ConfigDB_HasChanges("user")` holds (gui/options/options.js:355-358). Close asks first under the same test (:444-456). Revert is `ConfigDB_Reload("user")`, which goes back to the file on disk (:396-407). The flag belongs to the config database, not to the page, so it survives closing the page. For example, the FPS overlay and game-time counter toggles write the value without saving it (gui/common/OverlayCounter.js:34-38 `ConfigDB_CreateValue`). Options opened after that shows Revert and Save enabled, Revert switches the overlay back off, and Close asks about unsaved changes. The same happens after closing Options with unsaved changes ("Unsaved changes affect this session only", options.xml:58-61) and opening it again.
   - RTS0: `_ready` takes the current in-memory values as the saved ones (scenes/menu/options.gd:260-262, `_snapshot` :645-649). `has_unsaved_changes` compares against that snapshot (:651-655), and `_on_revert` restores it (:721-724). An unsaved toggle from fps.toggle or realtime.toggle (scenes/menu/user_settings.gd:139-143) or from the counters (src/presentation/session_input.cpp:427-431), or an earlier visit's unsaved edits, therefore leaves Revert and Save disabled. Revert cannot undo those changes, the page cannot save them, and Close doesn't ask.
   - Low-medium. Fix: keep a has-changes flag in user_config.cpp. `set` raises it when a value changes (:319-325), and `save` and `reload` clear it (:333-337, :367). Expose it on RtsoUserConfig. Have options.gd use it for `has_unsaved_changes`, and make `_on_revert` call `cfg.reload()` and then `_load_controls()`.
2. **Texture quality and Texture anisotropic filter have no item tooltips.**
   - 0.28: textures.quality items carry the tooltips "Low", "Medium" and "High", and textures.maxanisotropy items carry "Disabled", "2x", "4x", "8x" and "16x" (options.json, the textures.quality and textures.maxanisotropy lists in Graphics (advanced)). The hovered item shows its own tooltip (options.js:153-156). So hovering "1x" reads "Disabled".
   - RTS0: ITEM_TIPS has no entry for either key (scenes/menu/options.gd:184-235). Their items fall back to the option's description (:456-457).
   - Nit. Fix: add both keys to ITEM_TIPS: `{"0": "Low", "1": "Medium", "2": "High"}` and `{"1": "Disabled", "2": "2x", "4": "4x", "8": "8x", "16": "16x"}`.
3. **A dropdown whose saved value isn't in its list shows the first item.**
   - 0.28: valueToGui sets `selected = list_data.indexOf(value)` (options.js:146-148, :163-165). A value that isn't listed (a hand-edited user.cfg, `-conf=`, or a renderer.scale such as 0.6) selects nothing, so the box is blank.
   - RTS0: `_load_controls` starts `idx` at 0 and selects it when nothing matches (scenes/menu/options.gd:629-634). Such a value then reads "Sandbox", "50%", "Disabled" and so on, although the game still uses the unlisted value.
   - Nit. Fix: start `idx` at -1 and call `c.select(-1)` when nothing matches (RtsoSafeSelect/OptionButton accepts -1).
4. **The Fog row of Graphics (advanced) isn't indented.**
   - 0.28: that Fog row has dependencies (`rendererbackend != glarb`, options.json Graphics (advanced)), and any option with dependencies gets a label indented 25 px (options.js:347).
   - RTS0: indentation follows DEPENDENCIES (scenes/menu/options.gd:160-174, :402-408), which keys rows by config key, and "fog" is not in it. The repeated Fog row (:55) therefore sits flush left. Its condition can never fail in RTS0 (there is no glarb backend), so only the indentation differs.
   - Nit. Fix: indent by row instead: give the advanced Fog row (row id "fog#") the 25 px margin without making it depend on anything.
5. **Conquest Units defeats a player who starts with no units at once. 0.28 keeps them in, and has a second reason RTS0 lacks.**
   - 0.28: ConquestUnits.js:3-10 adds two queries: "Unit+!Animal" ("%(player)s has been defeated (lost all units).") and "ConquestCritical Unit+!Animal" ("%(player)s has been defeated (lost all units and critical structures)."). Spaces are OR and "+" is AND (globalscripts/Templates.js:72-103), so the second list holds critical structures, critical foundations included, as well as units. A query only fires when an entity leaves a non-empty list (ConquestCommon.js:17-27), and the lists are filled once at the start (:31-50). A player who starts with structures and no units, as a scenario can set up, is therefore not defeated by the first query. They stay in until their units and critical structures are all gone, and the chat then reads "lost all units and critical structures".
   - RTS0: `if ((vk & VK_CONQUEST_UNITS) && no_units) return SN::END_UNITS;` (src/simulation/simulation.cpp:18926) defeats such a player on the first check. The nomad "list started empty" rule that :18915-18922 and :18928-18931 apply to Structures and Civic Centers is missing here. kWhy in src/presentation/session_notices.cpp:168-171 has no "lost all units and critical structures".
   - Low. Fix: at :18926, require that the player held a unit (units at the start, or units_trained + units_captured + units_lost > 0, as `had_structure` does). Add a second test, `no_units && no critical structure or critical foundation && gone`, that returns a new END_UNITS_CRITICAL reason, and append "lost all units and critical structures" to kWhy.
6. **The second Conquest Civic Centers query ignores critical foundations.**
   - 0.28: "ConquestCritical CivilCentre+!Foundation" (ConquestCivicCentres.js:7-10) is ConquestCritical OR (CivilCentre AND NOT Foundation). The Foundation filter merges Identity, so a placed barracks or civic-centre foundation keeps ConquestCritical (simulation/templates/special/filter/foundation.xml:17-19) and stays in that list. A nomad who has finished no civic centre and has lost every critical unit is still in while a critical foundation stands.
   - RTS0: `critical_count` counts only finished structures (src/simulation/simulation.cpp:18820). The test at :18931 therefore defeats that nomad ("lost all civic centers and critical units and structures") with a civic-centre foundation still standing.
   - Low. Fix: count critical structures including foundations in a separate tally (`is_conquest_critical(b.type)` without the build_progress test). Use it in the :18931 test, and in item 5's new test. Conquest itself keeps the finished-only `critical_count` ("ConquestCritical+!Foundation", Conquest.js:3-6).
7. **A captured Gaia army tent keeps a player in a Conquest match.**
   - 0.28: the maps' tents (structures/tent_arab, tent_bedouin, tent_desert, tent_mace and tent_rome) take their parent from template_structure_special, which isn't civic or military, so they aren't ConquestCritical. rome/tent comes from template_structure_civic_house and is (template_structure_civic.xml:4).
   - RTS0: all five map tents load as BLD_TENT (src/simulation/sim_types.h:636-638). BLD_TENT is rome/tent's type (:517), and `is_conquest_critical` doesn't exclude it (:673-687). A player who captures a Gaia tent (Saharan Oases, Libyan Oasis, Cycladic Archipelago 3p) and loses every other critical entity is not defeated under Conquest.
   - Low. Fix: give the map tents a per-building flag that drops ConquestCritical, set at map load as the STRUCT_OFF_* bits are for the Uffington Horse (`map_structure_off`, src/presentation/session_setup.cpp:100-107). Test the flag next to `is_conquest_critical` at simulation.cpp:18820.
8. **The wonder, relic and Last Man Standing lines aren't sorted with the other countdowns.**
   - 0.28: every countdown is a time notification. GuiInterface.AddTimeNotification sorts them all by end time, latest first (simulation/components/GuiInterface.js:800), and TimeNotificationOverlay prints them in that order (gui/session/TimeNotificationOverlay.js:15-36). Under Last Man Standing, "Last remaining player wins." (12 hours) is on top. Two wonders list the later-ending one first. A map's 10-second text notification sits under a 20-minute wonder countdown.
   - RTS0: the overlay prints the trigger notifications first, then the text notices, then the wonder lines in building order, then Last Man Standing, the relic line and the ceasefire line (src/presentation/session_lifecycle.cpp:1199-1285).
   - Low. Fix: collect every line with its end time (the wonder's `wonder_duration - wonder_hold`, the relic's `relic_duration - timer`, Last Man Standing at 12 h, the ceasefire's), sort the whole set latest first, then join.
9. **"Last remaining player wins." follows the Last Man Standing box, not allied victory.**
   - 0.28: the line is added only when `!this.alliedVictory` (simulation/components/EndGameManager.js:161-180). Setup.js sets alliedVictory to LockTeams || !LastManStanding and only warns when both are set (simulation/helpers/Setup.js:63-65). With locked teams, the allies win together and the line never shows.
   - RTS0: the overlay tests `sim->last_man_standing()` (src/presentation/session_lifecycle.cpp:1249), although the victory check uses `allied_victory()` (src/simulation/simulation.h:1363, D10). With both settings on (a scenario or -autostart; Game Setup clears Last Man Standing once teams lock), the line shows, and the allies then win together.
   - Nit. Fix: test `!sim->allied_victory()` at :1249.

## Checked and matching

- **Win and defeat flow:**
  - EndGameManager.MarkPlayerAndAlliesAsWon, MarkPlayersAsWon and AlliedVictoryCheck: who wins (active mutual allies, or the owner alone under Last Man Standing), the beaten players named only for wonder and relic wins, and "last player alive" or "last players alive"
  - Player.SetState: entities to Gaia in two steps, winners revealed, defeat is permanent; resign is Commands.js "resign", "%(player)s has resigned."
  - Every defeat reason string except item 5's
- **Conquest:**
  - The ConquestCritical class tables checked against every structures/* template's resolved classes: towers, walls, palisades, economic structures, the forge, stoa, library, theatre, pyramids, hall, Laozi gate and the triumphal arch are not critical; civic and military structures, houses, wonders, the colonies and the encampment are
  - Critical units are every unit but catafalques, traders, healers and fishing and merchant ships
  - CivilCentre includes the military colony but not the German encampment ("-CivilCentre" in its VisibleClasses)
- **Wonder:**
  - No Gaia or foundation wonder counts: the foundation filter drops the Wonder component
  - The Gaia Stonehenge and Uffington Horse count once captured: both are template_structure_wonder (the Mausoleum and the Colossus are on no 0.28 map)
  - A countdown per wonder, restarted on an owner change or a change of the owner's exclusive mutual allies under allied victory
  - The three message sets and their audiences
- **Capture the Relic:** the winners, the restart rules, and the two messages named after the first relic's owner.
- **Regicide:**
  - The hero spawned at the start; defeat only when that hero dies or changes owner
  - An ungarrisonable hero boards nothing, and with Hero Garrison off it spawns on land beside a starting ship
- **The player's end of the match:**
  - QuitConfirmationDefeat: VICTORIOUS!/DEFEATED!, "You have won!"/"You have been defeated!", and "Do you want to quit?" withheld from a host while other humans play
  - The other quit questions: QuitConfirmationMenu's three cases, ResignConfirmation and ReturnQuestion
- **Game description lines:** Endless Game, the Wonder and Capture the Relic titles with minutes, Relic Count, Hero Garrison or Exposed Heroes, Locked Teams or Diplomacy, Last Man Standing or Allied Victory, and Ceasefire.
- **Options page:**
  - All 9 tabs and their tooltips. Every option's key, label, tooltip, type, list labels and values, and range, apart from the deliberate omissions (G36, G47) and the VSync tooltip (G47)
  - The dependencies, except item 4's indentation
  - The gui.scale 5-second keep question
  - The slider ("Value: … (min: …, max: …)"), number ("Min: …, Max: …") and colour ("Default: …") tooltip lines
  - The invalid-value warning on Save
  - Reset erasing only the page's keys and saving
  - The four buttons' captions and tooltips, the "Warning" No/Yes boxes and their texts
  - Every option applying live through the config generation
  - Defaults: equal to the cached default.cfg except the deliberate windowed and lobby.tls, and the RTS0-only lobby server and ports (inferred, as that file's version is unconfirmed)

## Status

All nine are fixed on main c0b327184:
1. RtsoUserConfig has a has-changes flag, as ConfigDB_HasChanges. Setting a different value raises it, and save and reload clear it. Options uses that flag for Revert, Save and the Close question, and Revert reloads user.cfg. e2e_options.
2. Texture quality and anisotropic filter items carry 0.28's tooltips. e2e_options.
3. An unlisted value leaves its dropdown blank (RtsoSafeSelect.deselect). e2e_options.
4. Graphics (advanced)'s Fog row is indented.
5. Conquest Units needs a player to have held a unit, and adds "lost all units and critical structures" (END_UNITS_CRITICAL). test_victory.
6. The second Conquest Civic Centers query counts critical foundations. test_victory.
7. The maps' army tents carry STRUCT_NOT_CRITICAL. This is a template_off bit, so there is no save change. test_victory.
8. Every countdown line is sorted by its end time, latest first.
9. "Last remaining player wins." shows only without allied victory.
