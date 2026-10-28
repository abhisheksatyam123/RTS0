# Petra attacks, pass 4: the attack manager (Formations)

Audit of 2026-10-28 against main 227e15c80, tracked as G97 in `docs/REMAINING_GAPS.md`. Sources: 0.28 `attackManager.js` (AM), `attackPlan.js` (AP), `headquarters.js`, `victoryManager.js`. Passes 1-3 and G42/G75/G94 are not repeated.

## Fixed (no save change)

- **M1 civic-centre retake has the captor as its target player** (AM:753, 793). `petra_retake_civic_centres` left `target_player` unset and didn't count the plan name, so after the retake the army could turn on another enemy (and overwrite currentEnemyPlayer), and peace with the captor didn't cancel it. Now `target_player = ev.to` and `plans_made` counts it. Low-medium.
- **M2 a new Attack/HugeAttack doesn't fix currentEnemyPlayer when it is made** (AM:403, AP:826-833). The provisional target pick at creation noted the enemy and could start the 240 s Gaia-relic wait minutes before launch. `find_petra_attack_target(..., note = false)` for that pick. Low-medium.
- **M3 a rush keeps the current enemy whatever its towers** (AM:548-552): the >6 towers veto bars only a new pick. Low.
- **M4 peace re-targets a gathering plan instead of aborting it** (AM:682-685, AP:457-461): a plan completing at its rally point only forgets its target player; its completing step then finds the target no longer an enemy's and goes back to preparation. Started attacks are still aborted. Low.
- **M5 base and barracks tests** (AM:365, 399; basesManager.js hasPotentialBase): a military colony counts as a potential base, and the academy and mercenary camps count as barracks for plan creation. Low.

## Open

- **O1 raid target order** (AM:425-433): 0.28 raids the first enemy entry of targetList in discovery order; RTS0 the one nearest the main base. `petra_defense_targets_` is kept sorted by id, so this needs a save change. Low. **Fixed** (save 132): `petra_defense_targets_` keeps discovery order (targetList.push), raids and switchToAttack walk it in that order.
- **O2 raid cadence**: a gathering raid holds back the next raid until it marches (0.28 doesn't count it as unexecuted), and a raid whose target failed is replaced after 6 s rather than the same turn. Low.
- **O3 plan names**: relic expeditions (victoryManager.js:714) don't take a plan name, so later plans' siege class (name % 3) can differ. Nit.
- **O4 turn order**: plans are made before started attacks update (0.28 after, AM:363), and ally attack requests are answered outside the attackManager gate, so a Sandbox AI answers in chat where 0.28 is silent. One AI turn. Nit.
