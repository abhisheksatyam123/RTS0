# Triggers: fourteenth pass against 0 A.D. 0.28, scenario test scripts (2026-10-21)

## Method

maps/scenarios/triggers_demo.js, pickup_test_map_triggers.js, unit_chasing_test_triggers.js, unit_dancing_test_triggers.js, unit_pushing_test.js, unit_motion_integration_test.js, treasure_islands.js and death_canyon_invasion_force_triggers.js were read line by line against their ports in simulation.cpp (the script table, trigger_pickup_test, trigger_unit_chasing, trigger_unit_dancing and their event handlers, the unit pushing and unit motion setups, Treasure Islands and Death Canyon), with each map's XML trigger points, players and settings against data/maps/*.json and data/map_types.json. Distances are compared after the 1 unit = 4 m and vision x0.28 scaling.

## Findings

| # | 0.28 | RTS0 before | Now |
|---|---|---|---|
| SC1 | fast_on_muskox: player 2's cavalry gets a queued forced attack on the muskox (unit_chasing_test_triggers.js:159-165) | `order_gather` on the living animal, which only resources match: the cavalryman stood idle | `order_hunt`, queued |
| SC2 | The muskox is QuickSpawned as player 1's, walks to (gx, 900) and is in the pair's onDelete list (:245-258) | A grazing Gaia muskox, not followed | Player 1's muskox, `order_fauna_walk` north, followed as TriggerWatch kind 2 so its death removes the cavalryman |
| SC3 | multiple_resources' chickens are QuickSpawn's player 1 (unit_motion_integration_test.js:287-289) | Gaia's | Player 1's |
| SC4 | The hooked dancer reports "not killed" from FindNewTargets, 100 ms into IDLE (:338) | A 500 ms poll from 3.5 s | 100 ms from 3.1 s |
| SC5 | The PerformGarrison hook sends the next walker as the first boards (pickup_test_map_triggers.js:171) | A 200 ms poll | Every tick |

## Matching

- triggers_demo: the loading warning, the nine event loggers, OnInterval (10 s delay, 5 s interval, off after three runs), the yes-no dialog with its captions and tooltips, OnRange on the three A points; the map's one Athenian player, 50k resources and A points.
- pickup_test_map: all ten experiments at 200 ms with their templates, counts, offsets and alternating spawn points, WalkTo(567, 53); the 15 trigger points match XML uids 14-28.
- unit_chasing_test: all 30 experiments in order, attacker_first, walk flags, queued forced attacks, the garrison variant, pairs deleted whole, no promotions, setup at 3 s.
- unit_dancing_test: all 26 experiments and their patterns, the hook on dancers[0], player 1's 5 m vision, no promotions, RemoveSpread at 150 s, the second run at 151 s, player 2 wins at 300 s.
- unit_pushing_test: all 16 experiments, the 90 m / 1260 m row wrap, the perf experiments left off as in 0.28, gather x100 for player 1, 266 spearmen in large_against_units, player 1 wins at 300 s.
- unit_motion_integration_test: all 22 experiments with their gx side effects, palisade lengths 14/9/4 m and rotations, the enclosed wall removed at 7 s, formation_attack on player 2's civic centre.
- treasure_islands: 36 treasures with the XML's mix, the intro at 2 s, the 18-18 tie message every 12 s after 10 s, the win at 19 after 5 s, the "+3" warnings.
- death_canyon_invasion_force: players 2 and 4 can't build; four players, locked teams, population limit 1 for 2 and 4, 100/102/6/6 units.

Already documented and not repeated: the marine archer and cavalry swordsman template substitutions and the storehouse not being a territory root (comments in simulation.cpp).
