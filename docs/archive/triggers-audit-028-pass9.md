# Triggers: ninth pass against 0 A.D. 0.28 (2026-10-20)

## Method

- **Petra diplomacy:** diplomacyManager.js and chatHelper.js were checked line by line. The gaps found are rows D1-D7 in REMAINING_GAPS "Triggers, ninth 0.28 pass". They were fixed in #1224, #1244, #1269, #1279 and #1292. D5 is still open on the attack side.
- **Trigger maps:** the 21 0.28 maps with a trigger script ran again headless on main at `083236697`, after the 10-20 merges (among them #1191 pushing and the Petra research order). The runs used `tools/probe_trigger_maps.cpp` with pass 8's arguments:
  - JSON maps ran 12 minutes.
  - Random maps ran 2 players at 256 tiles for 14 minutes.
- **Seed checks:** Death Canyon - Invasion Force also ran 8 minutes on seeds 11, 22, 33 and 44.

## Results

| Map | Pass 8 | Now | Verdict |
|---|---|---|---|
| Danubius, Flood, Elephantine, Survival of the Fittest, Wall Demo, Jebel Barkal, tutorials, Pickup, Triggers, Chasing, Dancing, Pushing and Units demos | timers, spawns and endings as scheduled | same timers and endings | match |
| Gallic Fields | waves at 60, 142, 273, 419 s (3, 3, 6, 6) | waves at 60, 201, 456 s (3, 6, 12) | match: the delay and size grow by one `randFloat(1, 3)` draw per wave, and the draws moved |
| Polar Sea | 3 wolf waves, Petra at 1 unit | 4 waves, Petra at 54 units | match: wave timing is random, and Petra's survival depends on it |
| Extinct Volcano, Cinema Demo, Motion Integration | Petra 162 / 31 / 18 units at the end | 139 / 52 / 0 | match variance (Petra's economy; in the motion test it is the target of 600 test units) |
| Death Canyon - Invasion Force | pass 6: p2's army gone by 4 min, p4 keeps 38 | the invasion armies clash. On seeds 33, 44 and the default, p2 wins and then takes p3's base by 4-6 min. On seeds 11 and 22, p3 holds | match: the two 80-100-unit Petra armies decide it, and the winner changes with the seed |

No differences from 0.28 were found in the trigger maps in this pass.

## Lobby ratings and scenario-only units

- **Lobby ratings.** `tools/lobby_server.py` get_rating_adjustment was compared line by line with lobby-bots `xpartamupp/elo.py`: the same constants (sure-win difference 600, K-factor rating 2200, volatility after 20 games, anti-inflation 0.015) and the same rounding. elo.py also refuses ratings below -2199, which no game reaches.
- **Scenario-only units.** Every template that the trigger scripts spawn in the runs above resolved to a roster template. No spawn was dropped for a missing one.
