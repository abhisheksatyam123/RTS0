# Petra attack logic: 0.28 vs RTS0 (Formations attack audit)

Status (REMAINING_GAPS G42):
- A1: fixed in Compare's getPopulationMax PR (the same four sites).
- A2, A3, A4: fixed.
- A5: the PLAN_SIEGE focus is gone; siege engines go with the squad on attack-move and pick the best structure near them. 0.28's full stop-and-repick of every unit at arrival is not ported: on test_petra's fortress map it walked the first Attack under three fortresses and lost it.
- A6: fixed (a citizen soldier leaving a plan joins the army ids: outOfPlan). Captured units are not marked yet.
- A9, A18: fixed (the rush finder measures from the army centre; a rush retargets on arrival).
- A12, A14, A15: fixed (loads dropped before rallying, the wonder/relic coin flip, the "other" answer).
- A13: kept. RTS0 queues a plan's batches straight in the buildings (0.28 holds them in its queue manager until sent), so only started batches match 0.28's queued entities; the 300 s bound stays.
- A11: fixed (a unit target is followed; one that garrisons makes its enemy holder the target).
- A10, A16, A17: fixed (the enemy is picked when the plan must start; an aborted army is released at once and withdraws to its rally point or a nearer base; the march path uses the large passability class).
- A7, A8: fixed (a raid that sets off joins the started attacks and another may be planned; started raids fight with the attack plans' per-unit loop). They still muster without a rally point or path.

Sources: 0.28 = Petra from the 0.28 release (simulation/ai/petra/*.js). RTS0 = src/simulation/simulation.cpp (sim.cpp; line numbers as of the audit, 10-20), simulation.h, petra_ai.h, sim_types.h.
Not repeated here: scale conversions, rushes in any phase, raid membership, siege for rushes and raids, G5, H4, D1-D11, L10.

Ordered by impact.

---

## A1. "Population max" means the hard cap in 0.28, but RTS0 reads the current housing limit (HIGH)
- **0.28**: attackManager.js:388-396 allows a new Attack/HugeAttack while `started < min(2, 1 + round(getPopulationMax()/100))` and (`started == 0 || getPopulationMax() - getPopulation() > 12`). attackPlan.js:504-530 treats "max pop reached" as `getPopulationMax() - getPopulation() < 5`, and lengthMin also comes from getPopulationMax(). `getPopulationMax()` is the hard cap (MaxPopulation, e.g. 300), not the housing limit: compare headquarters.js:1437 `if (getPopulationMax() <= getPopulationLimit())` and config.js:280.
- **RTS0**: sim.cpp:26174-26176 and 26322-26328 use `pop_max(petra_pid_)`. That function (sim.cpp:800-804) is `min(housing bonus, limit)`, i.e. 0.28's getPopulationLimit().
- **Effect**: (a) Whenever housing is within 5 of full, which happens often while houses go up, a mustering plan counts as "at max pop". It then starts as soon as every class reaches minSize, or with more than lengthMin units, and lengthMin itself shrinks. Attacks therefore launch early and small. (b) A second concurrent Attack is blocked whenever housing has 12 or fewer free slots. 0.28 blocks it only within 12 of the 300 cap. (c) At low housing, min(2, 1+round(x/100)) can drop to 1.
- **Save change**: no.
- **Fix**: use the hard cap (`pop_limit(petra_pid_)`, or `petra_config_max_pop`-style) in those three places instead of `pop_max`.

## A2. At max pop, 0.28 fails a plan that can't start; RTS0 keeps mustering (MEDIUM, tied to A1)
- **0.28**: attackPlan.js:504-529. At max pop, if `!canStart()` and the plan has no more than lengthMin units, updatePreparation returns PREPARATION_FAILED. The plan is aborted (attackManager.js:310-319) so its units can join other plans.
- **RTS0**: sim.cpp:26318-26329 (comment at 26302-26307). When capped and not ready, the plan just keeps mustering. The comment justifies this by RTS0 "sitting at its cap far more often", which is the A1 confusion.
- **Effect**: A plan stuck at the real cap holds its units indefinitely instead of releasing them and re-planning. In 0.28 the failed plan's units go back to outOfPlan and a new plan is made at once.
- **Save change**: no.
- **Fix**: after A1, set `state = UNEXECUTED` (abort) when capped, not `min_everywhere`, and `squad.size() <= length_min`.

## A3. A siege-less "Attack" that is ready to march falls back to defaultTargetFinder in RTS0 (MEDIUM)
- **0.28**: attackPlan.js:932-936. For TYPE_DEFAULT, getNearestTarget uses rushTargetFinder (an undefended structure). It falls back to defaultTargetFinder only `if (hasSiegeUnits() || this.forced)`. chooseTarget runs at mustStart (attackPlan.js:557-561). When nothing is found, the plan fails (PREPARATION_FAILED, then Abort).
- **RTS0**: sim.cpp:25322 `forced = p.forced || siege || HUGE || state == UNEXECUTED || state == MUSTER`. petra_nearest_target therefore passes the `!forced` early return at sim.cpp:25255, and the MUSTER-ready pick (sim.cpp:26359, via petra_plan_next_target to find_petra_attack_target) takes CCs, ConquestCritical structures and so on.
- **Effect**: Siege often can't be trained in the town phase, and every enemy structure may be within 80 m of a tower or the CC. In that case RTS0 sends an Attack at defended targets (often the civic centre) with no siege. 0.28 aborts the plan and re-plans until siege or an undefended target exists.
- **Save change**: no.
- **Fix**: treat only UNEXECUTED (the provisional pick at creation) as forced. At the MUSTER-ready pick, use `p.forced || siege || HUGE` only.

## A4. War elephants (Elephant+Melee) are not "siege" in RTS0's attack logic (MEDIUM)
- **0.28**: entityExtend.js:4-7 `isSiegeUnit = hasClasses(["Siege", "Elephant+Melee"])`. This feeds hasSiegeUnits (attackPlan.js:2194-2200), which drives the target-finder fallback (:935), the arrival test (:1887) and "fasten the end game" (:488). It also selects the siege branch of the per-unit loop (:1629-1678: elephants pick the most valuable structure in range and attack-move against structures only) and the Attacked reactions (:1396, :1428). Elephant+Melee is also one of addSiegeUnits' three classes (:402).
- **RTS0**: sim_types.h:1167-1169 `unit_is_siege_engine` is ram, catapult and bolt shooter only. RTS0 uses it in petra_attack_units (sim.cpp:25666 `siege` lambda), find_petra_attack_target (:25315), WALKING arrival (:26549-26553) and stuck-siege removal. MUSTER's siege count (:26293-26299) and the arrival siege split (:26641-26654) list only UNIT_CATAPULT, UNIT_SIEGE_RAM and UNIT_BOLT_SHOOTER. RTS0 still trains elephants as the plan's "siege" class (kPetraSiegeClasses[2], sim.cpp:19100).
- **Effect**: Elephants trained as an attack's siege fight as ordinary melee units chasing soldiers instead of knocking down structures. Their plan also doesn't count as siege-equipped: no defaultTargetFinder fallback, no end-game forceStart, and different arrival rules.
- **Save change**: no.
- **Fix**: add a `petra_is_siege_unit(i)` helper (`unit_is_siege_engine || (classes & (TC_ELEPHANT|TC_MELEE)) == both`) and use it everywhere attackPlan.js uses isSiegeUnit.

## A5. RTS0-only PLAN_SIEGE focus: siege is ordered straight at the target on arrival and on each retarget (MEDIUM)
- **0.28**: At arrival (attackPlan.js:1351-1369) every unit stops moving and `startingAttack` runs the per-unit loop over the whole army. Each siege unit picks the best structure within range+30 m (gate 10000, defensive fire 1000, ConquestCritical 200, then cost, random within the top 20%; :1629-1668), else attack-moves to targetPos against structures (:1669-1678). No plan type or "fortified" classification exists.
- **RTS0**: sim.cpp:25354-25360 sets `w.type` to PLAN_SIEGE / RAID / ASSAULT from "fortified" heuristics (towers, gates, CC hp > 800, wonder). At arrival (:26657-26664) and on retarget (:26740-26744), PLAN_SIEGE orders every siege engine to attack-building `target_id`. Those engines then never count as needing an update in petra_attack_units (only idle or unit-targeting units are re-picked), so they stay locked on the plan target.
- **Effect**: Rams and catapults ignore gates, towers and closer high-value structures in reach and walk to the one plan target. 0.28's engines take the nearest valuable structure, preferring gates when walled.
- **Save change**: no. AttackPlan::type is already saved and would become unused.
- **Fix**: drop the PLAN_SIEGE branch. On arrival, stop the army and run petra_attack_units with every member treated as `needs`, i.e. the full startingAttack pass.

## A6. Workers drafted into a plan don't become "outOfPlan" afterwards (MEDIUM)
- **0.28**: Every unit that leaves an attack plan gets `plan = -1` (removeUnit, attackPlan.js:2122-2135; also :598 and headquarters.js:244). This applies to workers taken by the keep/idle rule or by a rush, too. Defense restores -1 (defenseArmy.js:155-159). assignUnits then gives every plan-(-1) unit to the next plan with no keep or idle test (attackPlan.js:752-760, outOfPlan = attackManager.js:41). trainMoreWorkers counts only `plan === undefined` workers (headquarters.js:411), so it trains replacements. Units whose ownership changed also get plan -1 (headquarters.js:194-195).
- **RTS0**: Only infantry trained with PETRA_ORDER_ATTACK/RAID enter `petra_army_ids_` (sim.cpp:13124-13130), and nothing else ever adds to it. assignUnits applies the keep / per-base / idle rule to every other worker soldier (sim.cpp:26247-26251, 26283-26288). petra_train_more_workers counts such released workers as workers (sim.cpp:19677).
- **Effect**: After a rush (which drafts every worker past 8 regardless of task), 0.28's next Attack takes all those ex-rush workers back at once, even busy ones. 0.28 also trains replacement workers for them. RTS0 leaves them under the keep rule (only idle ones beyond keep), so later armies are smaller and the economy is larger than in 0.28.
- **Save change**: no. petra_army_ids_ is already saved (v55). Captured units would need the same treatment.
- **Fix**: when a unit leaves a plan (end of REGROUP/ABORT, petra_attack_remove_unit, a failed plan), insert its id into `petra_army_ids_`. Do the same on capture or conversion of a non-support unit.

## A7. Only one raid at a time in RTS0 (LOW-MEDIUM)
- **0.28**: attackManager.js:421-436 makes a new raid whenever no raid is *unexecuted* and defenseManager.targetList has an enemy target, taking the first one in list order. Raids in completing or started state don't block it, so several raids can run at once (all in startedAttacks[RAID]).
- **RTS0**: petra_manage_raid (sim.cpp:27012-27033) has a single `petra_raid_plan_` that re-plans only from UNEXECUTED/ABORT, after a 6 s wait, and then only after the previous raid's REGROUP. It picks the target nearest our base (sim.cpp:26972-26989).
- **Effect**: Against several enemy foundations or structures in our territory, 0.28 sends successive raids at once. RTS0 deals with them one after another.
- **Save change**: likely none if started raids move into `petra_started_attacks_` with kind PETRA_ATTACK_RAID (v52 saves each plan's kind). Otherwise a new saved list is needed.
- **Fix**: on launch, move the raid into petra_started_attacks_ (as the main plan does) and allow a new raid while none is mustering.

## A8. Started raids use a reduced combat routine (LOW-MEDIUM)
- **0.28**: Raids are ordinary AttackPlans once started. They go through completing (gather at the rally point, :565-566 maxCompletingTime +20 s), a path walk (UpdateWalking) and the full "" state loop (attackPlan.js:1372-1816): unit-target scoring, vetoes, help-the-nearest, structure scoring.
- **RTS0**: sim.cpp:27166-27178. A raid attack-moves straight from where its units stand, then only uses petra_answer_attackers (hit back, idle units attack-move) and makes idle riders within 10 units hit the target. There is no completing step, no path, and no petra_attack_units.
- **Effect**: Raiders don't pick off fleeing builders, supports or countered units with 0.28's scoring, and they don't gather before leaving.
- **Save change**: no.
- **Fix**: run raids through petra_update_attack_plan (MUSTER completing, WALKING, ATTACKED) with kind RAID.

## A9. A rush keeps its march target on arrival (LOW-MEDIUM)
- **0.28**: attackPlan.js:1360-1368. On arrival a Rush re-runs getNearestTarget from the army's current centre and switches to a better (nearer undefended) target.
- **RTS0**: The arrival pick goes through petra_plan_next_target (sim.cpp:26099-26108). While WALKING it returns the current target whenever that is alive, and the rush branch (:26114) would measure from our base anyway (find_petra_rush_target, sim.cpp:25545).
- **Effect**: A rush hits the structure it chose at the base, not the nearest undefended one it reached.
- **Save change**: no.
- **Fix**: on WALKING to ATTACKED for a rush, call petra_nearest_target(RUSH, …, army centre) and switch if it returns a target.

## A10. The enemy is fixed at plan creation, not when the plan must start (LOW-MEDIUM)
- **0.28**: targetPlayer stays undefined during preparation (attackPlan.js:33-35), unless an attack request sets it. chooseTarget calls getEnemyPlayer at mustStart (attackPlan.js:826-833), so wonder progress, relic counts and currentEnemyPlayer are read at that moment.
- **RTS0**: When a plan is made, find_petra_attack_target (sim.cpp:26182; rush: 26153) sets `w.target_player` and a provisional target. At the MUSTER-ready pick, find_petra_attack_target keeps that target's owner as the enemy while it stands (sim.cpp:25325-25331).
- **Effect**: An enemy who starts a wonder or takes the lead in relics during the muster (minutes) is not chosen. The creation-time enemy is attacked instead.
- **Save change**: no.
- **Fix**: at creation, reset `target_player = 0xFF` and `target_building_id = 0` after the provisional pick (or skip the pick). Keep target_player only when an attack request set it.

## A11. Unit targets: no garrison-holder switch, stale target position (LOW)
- **0.28**: UpdateTarget (attackPlan.js:1983-1998) refreshes `targetPos` every turn when the target is a unit. If the unit has garrisoned, the plan takes its enemy holder as the new target.
- **RTS0**: In ATTACKED (sim.cpp:26690-26704), a unit target (hero in regicide, relic) is only checked for being alive. `plan.target_x/z` keep the position from when it was picked, and a garrisoned hero counts as gone (not alive), so the plan retargets elsewhere.
- **Effect**: In regicide, the army attack-moves to where the hero was. When the hero shelters in a fortress, RTS0 turns to another target instead of the fortress.
- **Save change**: no.
- **Fix**: each turn, refresh target_x/z from the unit. If it is garrisoned in an enemy structure, retarget to that structure.

## A12. No resource drop-off before rallying (LOW)
- **0.28**: attackPlan.js:604-608. A unit carrying resources at completing first returns them (returnResources), then moves to the rally point with a queued order.
- **RTS0**: sim.cpp:26439 `order_walk(here…, staging)` directly.
- **Effect**: Drafted citizen soldiers march off with their loads, which are lost to the economy or delayed.
- **Save change**: no.
- **Fix**: for carriers, queue a ReturnResource to the nearest same-land dropsite before the walk (the worker.js returnResources path RTS0 already has at sim.cpp ~20290).

## A13. Training wait counts only started batches and gives up after 300 s (LOW)
- **0.28**: attackPlan.js:531-538. While `countOwnQueuedEntitiesWithMetadata("plan", name) > 0`, i.e. any batch queued in a building for the plan, started or not, the plan keeps going with no time limit.
- **RTS0**: sim.cpp:26345-26352 checks only `o.started` orders, and only while `state_timer < 300`.
- **Effect**: Queued, not yet started batches are abandoned at launch and their units arrive later as non-plan units. Rarely, a plan launches before its last batch.
- **Save change**: no.
- **Fix**: count every queued order with petra_plan == PETRA_ORDER_ATTACK, and drop the 300 s bound if the queue is known to drain.

## A14. Wonder plus relic victory: no random pick (LOW)
- **0.28**: attackManager.js:513-518. With both conditions, the relic enemy replaces the wonder enemy when `!enemyPlayer || randBool()`.
- **RTS0**: sim.cpp:25115-25124 returns the wonder enemy before the relic check.
- **Effect**: In wonder+relic games, RTS0 never goes for the relic holder while any enemy wonder exists.
- **Save change**: no (one draw from petra_rng_).
- **Fix**: when both apply and a wonder enemy was found, use the relic enemy if `petra_rng_` says so.

## A15. Attack request: no "other" answer (LOW)
- **0.28**: attackManager.js:82-88, 114-115. A completing plan aimed at another player sets `other`, and the answer is "other" (our own target) when fewer than 13 units are available.
- **RTS0**: sim.cpp:27613-27650 answers only join or decline. The comment at 27612 says RTS0 has no completing state, but `completing` exists now (simulation.h AttackPlan::completing). The join condition also requires `plan.target_building_id != 0`.
- **Effect**: Chat only. Allies are told "decline" instead of whom we are attacking.
- **Save change**: no.
- **Fix**: when `plan.completing && plan.target_player != target`, answer PETRA_CHAT_ATTACK_OTHER with player2 = plan.target_player. Drop the target_building_id test.

## A16. Abort holds the army; the withdrawal point ignores the rally point (LOW)
- **0.28**: Abort (attackPlan.js:2075-2113) withdraws to the plan's rally point if it is on the army's land, or to a nearer base anchor. Each unit is released at once (removeUnit: plan -1, role worker), so it is free for defense and the economy while walking.
- **RTS0**: ABORT (sim.cpp:26789-26822) picks only base anchors. REGROUP (sim.cpp:26755-26784) keeps the squad reserved (petra_unit_reserved) for up to 15 s on the same land, or 180 s while waiting for ships.
- **Effect**: Retreating units can't be grabbed by defense or by the next plan for a short while. They may also walk past the border rally point to a farther base.
- **Save change**: no.
- **Fix**: release the squad on entering REGROUP (move order only), and include plan.staging_x/z as a withdrawal candidate.

## A17. March path uses infantry passability, not "large" (LOW)
- **0.28**: attackPlan.js:1234 computes the march path with `getPassabilityClassMask("large")`.
- **RTS0**: sim.cpp:25004 `find_path(..., PASS_INFANTRY)`.
- **Effect**: Siege can be routed through gaps it can't pass, and the rally point (where the path leaves our territory) can differ.
- **Save change**: no.
- **Fix**: use the large or siege passability class for petra_plan_path.

## A18. The rush finder measures from our base, not from the mustered army (LOW)
- **0.28**: rushTargetFinder (attackPlan.js:1056-1058) measures distance from `unitCollection.getCentrePosition()`, falling back to the rally point.
- **RTS0**: find_petra_rush_target (sim.cpp:25544-25550) measures from the primary base.
- **Effect**: The rush can pick a different "nearest" undefended structure when the army has gathered away from the CC.
- **Save change**: no.
- **Fix**: pass the squad centre when it has units.

---

## Checked and matching
- setRushes thresholds and sizes (attackManager.js:45-62 / sim.cpp:25443-25471).
- Plan type order: rush while left and a barracks stands, otherwise Attack/Huge from the town phase or with no potential base; Huge from the third plan unless one is started (attackManager.js:365-419 / sim.cpp:26150-26190).
- unitStat tables, variation, the Easy/Very Easy shrink, popScaling (attackPlan.js:126-218 / sim.cpp:25514-25539, 19143-19192).
- addSiegeUnits class rotation by plan name and target sizes (attackPlan.js:396-448 / sim.cpp:19103-19108, 19194-19212).
- trainMoreUnits ordering (count/target − priority, +1000 when full) and queue choice (siege/hero, champion, default) (attackPlan.js:618-704 / sim.cpp:19341-19410). Queue priorities 250/150/90/70 with difficulty scaling (sim.cpp:27313-27320).
- mustStart (targetSize everywhere), Fasten the end game (20 + 2 × enemy units, every 5th turn), maxCompletingTime 40/60, attack-request post (attackPlan.js:325-353, 487-501, 565-576 / sim.cpp:26302-26339, 26441-26444).
- assignUnits keep formula, the per-base 4, rush vs idle rule, regicide hero health 0.8 (attackPlan.js:765-806 / sim.cpp:26233-26291).
- getEnemyPlayer: wonder progress, Gaia relics with lapse, rush veto above 6 towers/fortresses, currentEnemyPlayer, nearest CC on the same land, strongest +500 (attackManager.js:506-677 / sim.cpp:25113-25183).
- defaultTargetFinder tiers, field penalty, isValidTarget decay rule, checkTargetObstruction gate and blocker test (attackPlan.js:985-1222 / sim.cpp:25196-25427).
- getPathToTarget/setRallyPoint at the territory border with the dangerous-location step back (attackPlan.js:1224-1271 / sim.cpp:24993-25019).
- UpdateWalking arrival rules (attackedNB, 100 m, waypoint 40 m), stuck siege removal, wall stuck give-up (attackPlan.js:1866-1979 / sim.cpp:26536-26618).
- Per-unit "" loop: Attacked reactions, veto weights 8/5/3, ranges 60 / 30+range / 90, unit scoring (support 50, counter 100, −distance, veto −20000), structure scoring, help-the-nearest, 10 units a turn (attackPlan.js:1381-1811 / sim.cpp:25661-25887). Avoid sets per target type, except the documented wall/Fortress case.
- UpdateTarget: same land, help another started attack, then another enemy, else abort (attackPlan.js:2012-2068 / sim.cpp:26706-26748).
- assignBombers (range, 30 m safety, our territory and land, at most 5 per target, above Very Easy every 5th turn) (attackManager.js:142-250 / sim.cpp:25972-26008). Field bombing is documented as omitted.
- cancelAttacksAgainstPlayer, switchDefenseToAttack at 150 m with STATE_ARRIVED, numAttackingUnitsAround 130 m > 8 (attackManager.js:680-798 / sim.cpp:26827-26864, 28786-28800, 30351-30364).
- Regicide hero below high health leaves its plan (victoryManager.js:106-135 / sim.cpp:29842-29848).
