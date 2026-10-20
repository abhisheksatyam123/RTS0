# Petra garrisons, queues and workers, fourth pass against 0.28

Read-only audit of RTS0's Petra port (area C) against 0 A.D. 0.28 Petra (`garrisonManager.js`, `emergencyManager.js`, `chatHelper.js`, `queueManager.js`, `queue*.js`, `worker.js`, headquarters.js worker training). Items already in the earlier audits (`/mnt/project-files/ai/audit-028-pass3-economy.md`, `audit-028-trade-naval.md`, `petra-028-config-gaps.md`, `petra-cadence-audit.md`, `difficulty-audit-028*.md`, `phase1-stall-028.md`, `scenario-petra-028.md`) are not repeated. Every row of `audit-028-trade-naval.md` (T1-T11, N1-N7) is fixed on main as of 2026-10-20.

emergencyManager.js and chatHelper.js match 0.28 apart from the cadence items already listed (S1, S2).

| ID | Area | Impact | Save | Summary | State |
|----|------|--------|------|---------|-------|
| G1 | Garrisons | Medium-Low | no | Units still walking to a holder are never recalled once it no longer needs them (garrisonManager.js:181) | **Fixed** |
| G2 | Garrisons | Low | no | The enemy-nearby radius added the holder's half size to its arrow range; 0.28 measures centre to centre | **Fixed** |
| G3 | Garrisons | Low | no | Gaia was never a threat near a holder; 0.28 counts fighting Gaia units and shooting Gaia structures | **Fixed** |
| G4 | Garrisons | Low | no | Every hero kept the emergency rule; 0.28 uses it only for the critical (Regicide) hero | **Fixed** |
| G5 | Garrisons | Low | save | Any decaying barracks of ours is garrisoned and never scuttled; 0.28 garrisons only a barracks it captured while decaying | Open |
| Q1 | Attack training | High | no | Attack and raid units train one at a time (the plan queue asks for one unit's price); 0.28 trains full batches and asks for the batch plus 60% of the next | **Fixed** |
| Q2 | Accounts | Medium | no | The city-phase price was kept back from soldiers for the whole town phase; 0.28 saves for it only once phasing (workPhase3) | **Fixed** |
| Q3 | Phase research | Medium | no | While the phase's structures were missing, majorTech saved nothing and the phasing latch was not set; 0.28 queues the phase and saves for it, canStart waits for the structures | **Fixed** |
| Q4 | Accounts | Low-Medium | no | builderRatio and findBestTrainableUnit's 800 test used the whole stock, not getAvailableResources (stock less every account) | **Fixed** |
| Q5 | Plan queues | Low | no | A mustering raid and attack share the plan queues: the raid was funded at the attack's priority and their wants overwrote each other | **Fixed** (higher priority, larger want) |
| Q6 | Pausing | Low | no | A military colony did not count as a potential base for checkPausedQueues | **Fixed** |
| Q7 | Queue funding | Low | no | 0.28 funds and starts a plan queued this turn at the turn's end (queueManager.update after the managers), and its queues hold many worker plans; RTS0's trainMoreWorkers retries only on its next 4th turn | Measured, not landed (see below) |
| W1 | Gatherers | Medium | save | An idle gatherer is given pickMostNeededResources' pick; 0.28 keeps its gather-type and searches that resource again | **Fixed** |
| W2 | Builders | Medium | no | Builders carry on after a finish (gather at a new dropsite, or the next foundation); 0.28 sends autocontinue only for houses, so others go idle and are reassigned | **Fixed** (with W1) |
| W3 | Gatherers | Low | no | Supplies at danger >= 6 are skipped and idle soldiers at threat >= 7 are not reassigned; 0.28 has no danger map | **Fixed** |
| W4 | New bases | Low | no | A new civic centre's foundation gets up to 15 of the nearest builders; 0.28 moves 10 workers (bulkPickWorkers, 3 left per base) | **Fixed** |
| W5 | Builders | Low | no | Builders walk to a foundation still carrying their load; 0.28 queues a drop-off first | **Fixed** |
| P1 | Phase-up | Medium | no | getAccountedPopulation skipped started batches and counted each queued unit as 1; 0.28 counts every batch at its Cost/Population, a started one twice (popUsed holds its reserved slots). Found by Compare's pass 7 | **Fixed** |
| R7 | New bases | Low | no | buildNewBase's hasOwnCC counted any civic centre we hold; 0.28 counts only our own civ's civil_centre, so a Ptolemaic or Seleucid Petra holding only a captured one builds a civil centre, not a military colony. Found by Compare's pass 8 | **Fixed** |

## Fixes (this pass)

- **G1.** `petra_manage_garrisons` runs `petra_keep_garrisoned` over our units with a garrison order on the holder and stops those that fail (`tools/test_petra.cpp` test_garrison_recall: archers called to a Town Center turn back when the enemy leaves).
- **G2, G3.** `petra_around` uses the arrow range alone, and counts Gaia structures that shoot and Gaia units in combat.
- **G4.** `petra_keep_garrisoned` applies TYPE_EMERGENCY to `petra_is_critical_ent` only; other heroes go through the protection rules.
- **Q2.** The city-phase reserve on soldier training applies only while `petra_phasing()`.
- **Q3.** `petra_start_research` records the majorTech want for a phase that waits only on its structures.
- **Q4.** `petra_available_resource` (stock less every queue account) feeds builderRatio and both scarce-resource penalties.
- **Q5.** The plan queues take the higher priority of a mustering raid and attack, and the larger of their asks each turn.
- **Q6.** checkPausedQueues counts any base anchor (`petra_base_anchor_type`).

## Open items, details

- **G5.** garrisonManager.js:364 adds a barracks only through headquarters.js:170 (justCaptured). Fix: a saved set of barracks ids captured while decaying; `petra_decay_garrison_min` allows barracks only from it.
- **Q1.** Fixed: `train_for` trains the class's whole batch or nothing (attackPlan.js:688 `new TrainingPlan(..., max, max)`) and the plan queue asks for the batch plus 60% of the next while the class wants more (queue.js:72 maxAccountWanted). Petra against Petra, Continental 128, 4 seeds, 30 min: first attack 505 -> 469 s on average, population at 15 min 69 -> 75 and at 25 min 117 -> 140, and no player is wiped out by 20 min (before: one seed).
- **W1.** Fixed without a save change: an idle worker Petra had sent to a supply (worker.js metadata "supply", `petra_supply_`) takes the resource it gathered last (`res_type_carried`) as its gather-type and looks for it again (worker.js:222-238 startGathering), unless that resource is exhausted or failed in the last 20 s; `petra_order_build` clears the mark (subrole builder), so builders and new units still go through reassignIdleWorkers.
- **W2.** Fixed with W1: after building anything but a house a Petra builder goes idle for Petra to reassign; after a house it looks for a foundation nearby (worker.js:375 `repair(target, target.hasClass("House"))`, UnitAI ConstructionFinished). Alone (W2 without W1) three scenario checks went red and town/city were 10-13 s later. Together, Continental 8 seeds against an idle player: town 641 -> 646 s, city 1022 -> 1023 s. Fields now wait for a food gatherer (buildAnyField), so on test_petra's Nomad seed the first house comes at about 220 s instead of 80 s (the check runs 240 s), and test_mobile_dropsite_cadence stops the workers Petra sends toward its new base before checking that base has none.
- **W3.** Fixed: the gatherers' supply list and the idle-soldier reassignment no longer read RTS0's threat map; findSupply (worker.js:500) rejects enemy territory only. Continental, 8 seeds against an idle player: identical phase times (town 650 s, city 979 s), as nothing threatens the workers there.
- **W4.** Fixed: while a civic centre or military colony stands, a new base anchor's foundation wants 10 builders (basesManager.js:202 bulkPickWorkers(newbase, 10)), taken only from bases (workers by nearest standing anchor) with more than 3 left (baseManager.js:782); the first civic centre still takes every worker (test_petra test_new_base_builders). Continental, 8 seeds against an idle player: town unchanged (650 s), city 979 -> 982 s.
- **W5.** Fixed: `petra_order_build` drops the load at the nearest dropsite on the builder's land, the build queued behind it, and `petra_staff_foundations` counts such a builder for its foundation (test_petra test_builder_drops_load_first). Continental, 8 seeds: town 636 -> 629 s, city 959 -> 953 s.
- **P1.** Fixed: `petra_accounted_population` adds `order_pop` for every batch in our structures' queues (headquarters.js:2178; Player.js:138 TryReservePopulationSlots already adds a started batch to popUsed). Continental, 8 seeds against an idle player, with W4: town 650 -> 644 s, city 982 -> 996 s.
- **R7.** Fixed: `petra_build_new_base` counts a Town Center as ours only when its template civ is ours (headquarters.js:1566-1573; test_petra test_new_base_own_civ_cc).
- **Q7.** Tried 2026-10-20: funding at the turn's end with a same-turn worker retry. On Continental (8 seeds, Medium) the civic centre's idle share in the first 5 minutes fell from 36% to 34%, town (636 s) and city (959 -> 964 s) did not move, and 7-minute workers rose (17 -> 24 support). Five scenario checks that rely on early-game timing went red (test_petra defense armies, switch to attack, mobile dropsite cadence; test_naval concurrent transports). Compare's pass 6 found the same with its own retry. Not landed: the phase times say the early economy is limited elsewhere.
