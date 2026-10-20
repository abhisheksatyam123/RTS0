# Triggers: eighth pass against 0 A.D. 0.28 (2026-10-20)

## Method

- This pass is a regression check after the 10-16 to 10-19 merges. Those include the square-map edge ring (e2bd82ff3), moving-vs-idle pushing (b9498e350), cross-compiler determinism (#1151), the Petra city phase (#1159) and the arrow changes (#1164).
- The 20 0.28 maps with a trigger script ran again headless on main at `0b3f2cf1c`, using `tools/probe_trigger_maps.cpp` with the same arguments as pass 6:
  - JSON maps ran 12 minutes.
  - Random maps ran 2 players at 256 tiles for 14 minutes.
- The results were compared with pass 6's runs.

## Results

| Map | Pass 6 | Now | Verdict |
|---|---|---|---|
| Flood, Polar Sea, Survival of the Fittest, Danubius, Elephantine, tutorials, demos and test maps | timers, notes and spawns as scheduled | same | match |
| Extinct Volcano | rising lava timers 46/47 | 47/47; Petra ends with 162 units, was 172 | match (match variance) |
| Gallic Fields | waves at 60, 218, 612 s of `rome/champion_infantry_swordsman` | waves at 60, 142, 273, 419 s of `_02` | match: `_02` is pass 7's S1. The delays are `randFloat(1, 3)`, and the draws moved with other changes |
| Jebel Barkal | Petra kept 27 units at 600 s; the second attack had 59 units | Petra has 0 units at 600 s; the second attack at 791 s has 4 champion elephants | match, see below |

## Jebel Barkal check

The first attack at 403 s wipes Petra's army on most seeds, and on the build from before these merges too:

| Build | Petra units at 600 s, seeds 11 / 22 / 33 / 44 |
|---|---|
| pass 6 (before the merges) | 0 / 0 / 0 / 20 |
| now | 0 / 0 / 0 / 1 |

The pass-6 run that kept 27 units was a lucky seed. In 0.28 (`jebel_barkal_triggers.js` JebelBarkal_SpawnAttackerGroups, lines 543-549), a spawn group whose target classes match none of the players' entities is skipped. With no player units left, only the elephant group spawns, because it targets Structure. That gives the 4 elephants at 791 s.

No differences from 0.28 were found in this pass.
