# B audit pass 5: victory modes in depth, against 0 A.D. 0.28

Area: the victory-condition simulation (ConquestCommon, Regicide, CaptureTheRelic and WonderVictory scripts; EndGameManager, Player, CeasefireManager and TriggerHelper), read line by line. The following already match 0.28:

- the conquest-critical classes for each variant
- countdown restart rules, and every countdown message with its audience
- allied victory and Last Man Standing
- regicide spawn order and ungarrisonable rules
- garrisoned and shipped units counting as alive
- units ejected on defeat
- the ceasefire lines

## Differences

1. **Wonders the map places start counting at once.**
   - 0.28: WonderVictory.js:155-164 starts a countdown only on OwnershipChanged. Trigger scripts load after the map's entities, so a starting wonder never counts. Egypt 3v3 gives four players 5 wonders.
   - RTS0: every finished wonder counts from tick 0 (simulation.cpp, wonder countdown).
   - Medium. Needs a per-building flag, so the save format changes. Its own PR.
2. **Map relics are dropped, and the relic count trims them instead of adding.**
   - 0.28: CaptureTheRelic.js:15-21 spawns the relic count on top of the maps' Gaia catafalques (crocodilopolis_4p 3, north_african_savanna_2p 4, and one each on gallic_highlands_2p, magadha_2p and cycladic_archipelago_2p/3p).
   - RTS0: port_0ad_maps.py has no catafalque entry, and apply_relic_count trims to the setting.
   - Medium. The map port belongs to F.
3. **The regicide hero pool lacks Xenophon (Athens) and the 300 Leonidas (Sparta).**
   - 0.28: Regicide.js:13-32 takes every civ unit template with the Hero class.
   - RTS0: civ_roster_data.h has neither.
   - Low-medium. The roster belongs to D.
4. **Conquest Structures ignored placed foundations.**
   - 0.28: special/filter/foundation.xml keeps the Structure class.
   - Low-medium.
5. **Traders refused former enemies' markets during a ceasefire.**
   - 0.28: CeasefireManager.js:76-80 makes enemies neutral, and Trader.js:186 refuses only enemies.
   - Low.
6. **Allied victory needs a fight first ("fought").**
   - Kept: deliberate, so an all-allied sandbox doesn't end at once (pass 4 notes).
7. **The last players falling on one tick all lost.**
   - 0.28: each SetState is followed by AlliedVictoryCheck (EndGameManager.js:192-195), so the first defeat makes the others win.
   - Low.
8. **A regicide hero skipped a structure with no free spot.**
   - 0.28: TriggerHelper.js:154-165 SpawnUnits falls back to the structure's position.
   - Low.

## Status (2026-10-20)

- 4, 5, 7 and 8 are fixed in the pass-5 PR. tools/test_victory.cpp checks the foundation and the same-tick fall; test_trade checks the ceasefire market.
- 1 is next, as its own save PR.
- 2 and 3 are REMAINING_GAPS G48, handed to F and D.
