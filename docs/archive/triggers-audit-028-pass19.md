# Triggers: nineteenth pass against 0 A.D. 0.28, regression on main (2026-11-03)

## Method

- **Trigger maps:** the 21 0.28 maps with a trigger script ran headless on main at `a2e5e6721` with `tools/probe_trigger_maps.cpp` and pass 11's arguments (JSON maps 12 minutes; random maps 2 players at 256 tiles for 14 minutes).
- **Tutorials and scenario scripts:** `maps/tutorials/*.js` and `maps/scenarios/*.js` were compared with their ports by name and by the run above. Pass 16 covered the tutorials line by line and pass 14 the scenario test scripts, so this pass re-checks only the endings and spawns.

## Results

| Map | Pass 18 | Now | Verdict |
|---|---|---|---|
| Tutorials, Pickup, Triggers, Chasing, Motion, Treasure Islands, Cinema Demo, Units Demo, Flood, Elephantine, Extinct Volcano, Wall Demo | timers and spawns as scheduled, no ending | the same | match |
| Unit Dancing, Unit Pushing | end at 300 s | the same | match: 0.28's `EndGame` at 300 s |
| Survival of the Fittest | p2 wins at 368 s | the same | match |
| Danubius | p2 wins at 422 s | p1 wins at 441 s | match variance: the run's seed and the Gaul waves' random draws |
| Gallic Fields | 39 spawns | 27 spawns | match variance: each wave's size and delay come from `randFloat(1, 3)` |
| Death Canyon - Invasion Force | runs to 720 s | the same | match: the stall is the 0.28-correct allied units at p3's civic centre (C, pass 18 bisect withdrawn) |
| Jebel Barkal, Polar Sea | Petra keeps 12 and 3 units | 12 and 3 | match variance |

No map differs from 0.28 in a way this pass could confirm, so no code changed.
