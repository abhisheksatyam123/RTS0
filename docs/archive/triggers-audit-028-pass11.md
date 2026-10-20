# Triggers: eleventh pass against 0 A.D. 0.28 (2026-10-20)

## Method

- **Trigger maps:** the 21 0.28 maps with a trigger script ran again headless on main at `916aab852`, with `tools/probe_trigger_maps.cpp` and pass 9's arguments. This main includes the scenario-only units (#1392), the lobby fixes (#1406), the trigger-point spawn spots (#1387) and the other threads' 10-20 merges.
  - JSON maps ran 12 minutes.
  - Random maps ran 2 players at 256 tiles for 14 minutes.
- **Maps with new units:** the 20 maps that #1392 gave back the units the porter used to drop ran 6 minutes each. Units Demo and Death Canyon - Invasion Force ran with the trigger maps above.
- The probe now leaves out a unit with no type and no template to stand in for it, as `load_map_seeded` does. Before, it spawned Flight Demo's planes as a generic unit.

## Trigger maps

| Map | Pass 9 | Now | Verdict |
|---|---|---|---|
| Tutorials, Pickup, Triggers, Chasing, Dancing, Pushing and Motion demos, Flood, Elephantine, Extinct Volcano, Wall Demo, Treasure Islands, Cinema Demo | timers, spawns and endings as scheduled | the same | match |
| Danubius, Survival of the Fittest | p2 wins at 437 s and 368 s | p2 wins at 436 s and 368 s | match |
| Jebel Barkal, Polar Sea | Petra keeps 0 and 54 units | 1 and 1 | match variance (pass 8's seed table and pass 9's wave note) |
| Gallic Fields | 21 Roman spawns, none left alive at 12 min | 36 spawns, 22 still alive and owned by Gaia | match: `gallic_fields_3p.js` spawns them for player 0 (Gaia), and each wave's size and delay come from a `randFloat(1, 3)` draw |
| Death Canyon - Invasion Force | p1 and p2 win at 278 s; p2 lands 80 units | they win at 234 s; p2 lands 100 | match: p2's 20 mercenaries are back (SU1) |
| Units Demo | 17 player structures turned Gaia within 25 s | none do | match: the Han and Mauryan catafalques are back (SU1). Each one's global aura gives every structure +20% territory radius (`han_catafalque_1`, `maur_catafalque_1`), which brings the outlying structures into territory |

## Maps with new units

Every unit in these maps spawns, except Flight Demo's 10 planes and one `han/siege_shoushe` in Units Demo (SU3). We Are Legion's two armies, 137 and 140 units, start in reach of each other, and player 1 wins the battle at 39 s with 73 left. That is the map's purpose, a large-battle test. The other 19 maps stay open for the full 6 minutes, with no defeat and no unit stuck off the map or in the water.

No differences from 0.28 were found in this pass.
