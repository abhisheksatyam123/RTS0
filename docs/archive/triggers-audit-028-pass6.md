# Triggers: sixth pass against 0 A.D. 0.28 (2026-10-19)

## Method

- Every 0.28 map with `TriggerScripts` (21 maps, the list in the shared triggers/audit-028-pass3.md) was run again headless on main at `b9498e350` with `tools/probe_trigger_maps.cpp`. Random maps ran 2 players at 256 tiles for 14 minutes; scenario, skirmish and tutorial maps ran 12 minutes, Petra in every seat but player 1.
- Results were compared with pass 3's table and with the 0.28 map scripts, `jebel_barkal_triggers.js`, `danubius_triggers.js` and Petra's `entityExtend.js gatherTreasure`.
- H4 (several transport ships per overseas army) was found already on main as G14, so this pass took no Petra transport work.

## Results

| Map | Pass 3 | Now | Verdict |
|---|---|---|---|
| Death Canyon - Invasion Force | p2 (80 units, no base) sat idle | p2 attacks and is gone by 4 min; p4 keeps 38 units | matches 0.28 (attackManager.js:398, "no base ... nothing else to do than attack") |
| Jebel Barkal | patrols at 0 s and 5 min, first attack at 403 s | same; 92 of 125 start units turreted on walls | match |
| Danubius | timers as scheduled | same; Gaul ships unload at 346 s, CC attackers at 407 s | match |
| Every other map | match | match | match |

## Checked and found matching

- **Danubius, Petra losses at the Gaul village.** With 2 players at 256 tiles, Petra's CC is 33 tiles from a Gaul civic centre on the same ray (0.28 places both the same way: `playerPlacementRiver(startAngle, 0.6)` and `mapBounds.left + 18`, both rotated by `startAngle`). Petra sends each new worker to the village's treasures and the defensive Gauls kill it. RTS0 follows 0.28's `gatherTreasure` rules here: Gaia territory is allowed, there's a 14000 m² cap outside its own land, and a 20 s wait between tries (simulation.cpp:20882-20902). So this is 0.28 behaviour and was not changed.
- **Jebel Barkal per-difficulty garrisons.** Wall towers hold champions only above Medium, and wall segments turret `(difficulty - 2) / 3` of their capacity. Wall towers carry `WallTower` but not `Tower` in 0.28 as in RTS0, so they never take the tower entry.

## Probe fix

- The probe counted wall-turreted units as neither garrisoned nor moving, so it reported 100 "stuck" Jebel Barkal spawns. It now counts them with garrisoned units and leaves them out of the stuck list.

No differences from 0.28 were found in this pass.
