# Triggers: seventeenth pass against 0 A.D. 0.28, regression and Jebel Barkal (2026-10-28)

## Method

- **Trigger maps:** the 21 0.28 maps with a trigger script ran headless on main at `c0b327184` with `tools/probe_trigger_maps.cpp` and pass 11's arguments (JSON maps 12 minutes; random maps 2 players at 256 tiles for 14 minutes).
- **Every scenario map:** all 155 `data/maps/*.json` maps ran 4 minutes each, to check for crashes, early endings, units off the map or in the water, and script spawns that never move.
- **Jebel Barkal (SW4):** `jebel_barkal_triggers.js` was read line by line against `trigger_jebel_barkal_*`, and a 4-Petra match (map 14, 256 tiles, Medium, seed 7) was logged each minute: waves, their make-up, and each death's killer and place. The other scripts' patrol commands (`danubius_triggers.js`) were checked for the same thing.

## Results

| Map | Pass 11 | Now | Verdict |
|---|---|---|---|
| Tutorials, the scenario demos, Flood, Elephantine, Extinct Volcano, Wall Demo, Treasure Islands, Cinema Demo | timers, spawns and endings as scheduled | the same | match |
| Danubius, Survival of the Fittest | p2 wins at 436 s and 368 s | 442 s and 368 s | match |
| Death Canyon - Invasion Force | p1 and p2 win at 234 s | 226 s | match |
| Gallic Fields | 36 spawns | 21 spawns in waves of 3, 3, 6 and 9 at 60, 121, 262 and 509 s | match: each delay and size is the last one times `randFloat(1, 3)`, checked against the vars |
| Jebel Barkal, Polar Sea | Petra keeps 1 and 1 units | 18 (after the SW4 fix below; 0 before it) and 94 | match variance |
| The 155 scenario maps | | none crashes, none ends early (We Are Legion's battle at 41 s and Death Canyon at 226 s are their designs), no unit off the map | match |

## Jebel Barkal

The first attack comes at 6-7 minutes and the second at about 13, as `jebelBarkal_firstAttackTime` and `jebelBarkal_attackInterval` give. Each wave's size follows `groupSizeFactor` (4 players / 30 spawn points x 3 x 0.85 = 0.34) times each row's `scaleByTime` count, and its make-up matches: basic citizen soldiers at first, champion elephants from the elephant stables and the wonder, and no hero yet. Unit health matches too, for example the champion elephant's 900 (1000 x 0.9 for `elephant_african`).

Before the first wave the Petras lose 2-5 units a minute each. They die at Napata's walls and gates, to the turreted archers and javelinists, while collecting the treasures `jebel_barkal.js` places in the city. That is 0.28's `gatherTreasure` too: it skips only enemy territory, and Gaia's is no one's. The four Petras also fight each other from minute 4.

The difference was in the patrol commands' `targetClasses`. RTS0's patrols took units only, so the elephants, which 0.28 sends after `Structure` or `Defensive SiegeEngine Monument Wonder`, trampled workers instead. No 0.28 template has a `SiegeEngine` class (siege is `Siege`), and no unit is `Defensive`, `Monument` or `Wonder`, so both lists take structures only. The script's target list for the second choice took siege engines too; it now takes structures only.

Fixed (SW4): an individual patrol keeps the script's classes in its order flags.

- Napata's elephants draw their list again for every leg, as 0.28 calls `targetClasses()` once per command.
- Its other attackers and city patrols take `Unit+!Ship`.
- Danubius's siege engines take `Defensive` structures and its ships take warships.

On seed 7 the four Petras keep 21, 17, 13 and 5 units at 10 minutes (before: 16, 1, 3 and 3); the growing later waves still overrun them. Kept: a patrol in formation takes any unit, not `Unit+!Ship`.
