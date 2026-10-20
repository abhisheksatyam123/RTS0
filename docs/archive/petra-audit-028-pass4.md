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
| Q1 | Attack training | High | no | Attack and raid units train one at a time (the plan queue asks for one unit's price); 0.28 trains full batches and asks for the batch plus 60% of the next | Open (needs a soak comparison: deliberate RTS0 choice so the villager queue keeps its food) |
| Q2 | Accounts | Medium | no | The city-phase price was kept back from soldiers for the whole town phase; 0.28 saves for it only once phasing (workPhase3) | **Fixed** |
| Q3 | Phase research | Medium | no | While the phase's structures were missing, majorTech saved nothing and the phasing latch was not set; 0.28 queues the phase and saves for it, canStart waits for the structures | **Fixed** |
| Q4 | Accounts | Low-Medium | no | builderRatio and findBestTrainableUnit's 800 test used the whole stock, not getAvailableResources (stock less every account) | **Fixed** |
| Q5 | Plan queues | Low | no | A mustering raid and attack share the plan queues: the raid was funded at the attack's priority and their wants overwrote each other | **Fixed** (higher priority, larger want) |
| Q6 | Pausing | Low | no | A military colony did not count as a potential base for checkPausedQueues | **Fixed** |
| Q7 | Queue funding | Low | no | 0.28 funds and starts a plan queued this turn at the turn's end (queueManager.update after the managers), and its queues hold many worker plans; RTS0's trainMoreWorkers retries only on its next 4th turn | Measured, not landed (see below) |
| W1 | Gatherers | Medium | save | An idle gatherer is given pickMostNeededResources' pick; 0.28 keeps its gather-type and searches that resource again | Open |
| W2 | Builders | Medium | no | Builders carry on after a finish (gather at a new dropsite, or the next foundation); 0.28 sends autocontinue only for houses, so others go idle and are reassigned | Open |
| W3 | Gatherers | Low | no | Supplies at danger >= 6 are skipped and idle soldiers at threat >= 7 are not reassigned; 0.28 has no danger map | Open |
| W4 | New bases | Low | no | A new civic centre's foundation gets up to 15 of the nearest builders; 0.28 moves 10 workers (bulkPickWorkers, 3 left per base) | Open |
| W5 | Builders | Low | no | Builders walk to a foundation still carrying their load; 0.28 queues a drop-off first | **Fixed** |

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
- **Q1.** attackPlan.js:688 `new TrainingPlan(..., max, max)`, queue.js:72 account target batch + 0.6 next; RTS0 `train_for` asks for one unit's price and tries batches from `batch` down to 1, so the account never holds more than one unit.
- **W1.** worker.js:222-238, 443-460; RTS0 seeds idle workers with `gather_type = RES_COUNT`. Fix needs a saved per-unit gather type.
- **W2.** worker.js:375 `repair(target, target.hasClass("House"))`; RTS0 `next_after_construction` harvests for every Petra builder.
- **W3.** findSupply (worker.js:500) rejects enemy territory only.
- **W4.** basesManager.js:202 bulkPickWorkers(newbase, 10); baseManager.js:780 keeps 3.
- **W5.** Fixed: `petra_order_build` drops the load at the nearest dropsite on the builder's land, the build queued behind it, and `petra_staff_foundations` counts such a builder for its foundation (test_petra test_builder_drops_load_first). Continental, 8 seeds: town 636 -> 629 s, city 959 -> 953 s.
- **Q7.** Tried 2026-10-20: funding at the turn's end with a same-turn worker retry. On Continental (8 seeds, Medium) the civic centre's idle share in the first 5 minutes fell from 36% to 34%, town (636 s) and city (959 -> 964 s) did not move, and 7-minute workers rose (17 -> 24 support). Five scenario checks that rely on early-game timing went red (test_petra defense armies, switch to attack, mobile dropsite cadence; test_naval concurrent transports). Compare's pass 6 found the same with its own retry. Not landed: the phase times say the early economy is limited elsewhere.
