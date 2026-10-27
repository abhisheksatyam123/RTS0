# Petra attack plans: 0.28 vs RTS0 (parity audit, attack pass 2)

## Status (Formations, G75)
- **P2 fixed**: each plan waits only for its own units' ferry (`petra_plan_in_transport`), and the drop point comes from the transport just started.
- **P3 partly fixed**: an army that finds no target on arrival goes through Abort (rally point or nearest base on its land), not straight home. Still open: an aborted army on another land is held and shipped home (0.28 frees it where it stands).
- **P4 fixed**: a target switch orders only idle soldiers; targetPlayer follows the new target's owner (`test_retarget_keeps_fights`).
- **P6 fixed**: a new rush has no targetPlayer and notes no current enemy until it must start; a forced rush, or one with siege engines, falls back to defaultTargetFinder (`test_rush_target_player`).
- **P7 fixed**: while part of an overseas army still sails, the first Attacked event turns its idle landed units on the attacker; it attacks as soon as none of it is in a transport (`test_petra_landed_army_answers_attack` in test_naval).
- **P1 part 1 fixed**: a completing plan moves to the started attacks at once (still MUSTER + completing) and the next plan is made and trains while it gathers. A completing plan that loses its target there releases its units to the plan in preparation (0.28 prepares it again beside the new one). `launched[]` and the launch chat use the plan's own kind; helping another attack and isDefendable skip completing plans; attack requests count them.
- **Open**: P1 part 2 (an Attack preparing beside the last rush needs a second preparing plan and a save bump), P5 (overseas preparation).
- Line numbers below are from the tree the audit read (main 7c93a723e).


Read-only audit. 0.28 = /mnt/project-files/ai/0ad-028-petra/*.js. RTS0 = src/simulation/simulation.cpp (sim.cpp), simulation.h. Line numbers are from the current tree.
Not repeated: FORMATIONS_ATTACK_AUDIT_028 A1-A18 (A5 and A13 kept on purpose), the defense and towers/relics audits, G14/G42/G49, the scale conversions, the 1.6 s turn, started attacks keeping their slot, siege-less Attack hitting only undefended structures. Units trained for a plan joining it (attackPlan.js:689-695, headquarters.js:237-245, assignUnits recruiting) are left out because another thread owns them.

Ordered by impact.

---

## P1. Only one plan can prepare at a time; the next plan waits until the current one marches (MEDIUM)
- **0.28** attackManager.js:283-301 counts only plans in STATE_UNEXECUTED as "unexecuted". attackManager.js:365-419 creates a new Rush when `unexecutedAttacks[Rush] == 0`, and a new Attack/HugeAttack when `unexecutedAttacks[Default] == 0 && unexecutedAttacks[Huge] == 0` and the started-count/free-pop test passes. The test counts only `startedAttacks`, and a completing plan is still in upcomingAttacks. Two things follow:
  - When a plan enters STATE_COMPLETING (it gathers at the rally point for up to 40/60 s, attackPlan.js:553-566), the next plan is created at once. With 0 attacks under way that is immediate. It starts assignUnits and trainMoreUnits while the first army is still gathering.
  - After the last rush is created (`rushNumber == maxRushes`), the `else if` branch can create an Attack the next turn (town phase) while that rush is still preparing. The two plans prepare at the same time.
- **RTS0** has one preparation slot, `petra_attack_plan_` (simulation.h:3612). A plan is created only from its UNEXECUTED state (sim.cpp:29994-30068). The slot is freed only when the plan leaves MUSTER, i.e. at `petra_start_march` after completing (sim.cpp:29911-29915, completing at 30080-30099). Per-plan data also lives in the manager: `petra_attack_mgr_.kind / attack_siege / dropped_classes / target sizes`.
- **Effect**: every Attack/HugeAttack (and every rush after the first) starts mustering and training 40-60 s later than in 0.28, and longer when the completing plan waits on a ferry (P2). In 0.28 the second army's training overlaps the first army's gathering, so attack waves come closer together. An Attack can never prepare alongside the last rush. In 0.28 the two compete for units, which changes how big the rush is.
- **Save change**: the completing overlap can be fixed without one. The rush+Attack overlap needs one (a second preparing plan, with kind/siege/dropped/sizes stored per plan instead of in petra_attack_mgr_).
- **Fix sketch**:
  1. (No save change.) When MUSTER sets `completing = true` (sim.cpp:~30330), move the plan into `petra_started_attacks_` at once, still in MUSTER+completing. Run the completing branch for started plans too. Exclude MUSTER plans from the `started` count at sim.cpp:30033-30038. In `petra_start_march` and the completing code, use `plan.kind`, not `petra_attack_mgr_.kind` (chat, `launched[]`, completing_time). The save loader (sim.cpp:36927) must accept MUSTER in the started list.
  2. (Save change.) Turn `petra_attack_plan_` into a small vector of upcoming plans, with the per-plan muster fields moved from PetraAttackManager into AttackPlan. Create an Attack when no upcoming Attack/Huge is still UNEXECUTED, even while a rush is upcoming.

## P2. Every attack shares one transport "owner", so any attack ferry blocks every attack plan (MEDIUM on naval maps, none on land maps)
- **0.28** attackPlan.js:2186-2192 `waitingForTransport` looks only at *this plan's* units with `transport` metadata. The completing step (attackPlan.js:465-466) waits only for those. A transporting army is "done" once none of *its* units is in transport (UpdateTransporting, attackPlan.js:1823-1841).
- **RTS0** tags every attack's transport plan with the same owner, `PETRA_TRANSPORT_ATTACK` (petra_ai.h:251). That covers the outgoing crossings of every attack and the homeward ferries of aborted or regrouping armies (`petra_go_home`, sim.cpp:30659, 30734). `petra_transport_for(owner)` returns the first such plan (sim.cpp:26283-26287). It is used as follows:
  - sim.cpp:30094: a completing plan waits while *any* attack transport exists. This comes before the `completing_time` test, so it is unbounded.
  - sim.cpp:30381: a WALKING army that has landed stays "still at sea" (`return`) while any attack transport exists.
  - sim.cpp:30357: after `petra_start_transport`, the plan's staging (drop) point is read from `petra_transport_for(...)`, which is the *first* attack transport, not the one just pushed at `petra_transports_.back()`.
- **Effect**: on naval maps, a land attack's gathering, or a landed overseas army, is held for as long as another army is being ferried. That includes survivors being shipped home: REGROUP waits up to 180 s. A second concurrent attack across the same sea freezes the first one on the beach until its own crossing ends. The drop point can belong to another army's crossing. Arrival still falls back on the 18 s timer, so the main cost is the waits.
- **Save change**: no, for the fix below. Tagging each transport with a plan id would need one.
- **Fix sketch**: replace the three owner tests with a per-plan test like 0.28's: `any_of(plan.squad, petra_passenger(uid) || petra_aboard(uid))`. At sim.cpp:30357 take the drop point from `petra_transports_.back()` right after a successful `petra_start_transport`, which is what the land branch at 30313 already does.

## P3. Withdrawal: an overseas army is held and shipped home; "no target on arrival" ignores the rally point and nearer bases (LOW-MEDIUM)
- **0.28** Abort (attackPlan.js:2075-2113) looks for a withdrawal point only on the army's own land (getAttackAccess): the rally point if it is there, else the nearest base anchor there. If there is none, units just `stopMoving()`. Every unit is released at once (`removeUnit`: plan -1, which is outOfPlan). An army stranded on an enemy island is therefore free straight away, and the next plan's assignUnits takes it (isAvailableUnit accepts any land). That plan ferries it to its rally point when it completes. The same Abort runs whatever the cause: no new target after arrival (UpdateTarget false), walls, an empty army, cancelAttacksAgainstPlayer.
- **RTS0**:
  - ABORT (sim.cpp:30701-30737) uses the primary base (`base_x/z`) as the default even when it is on another land. `petra_go_home` then asks for a ferry. REGROUP (sim.cpp:30653-30697) releases only the units already on the destination land. Units elsewhere stay in the squad, reserved, until the ship brings them, for up to 180 s (sim.cpp:30683-30686).
  - When the arrival pick finds no target (sim.cpp:30530-30536), and when the army dies out (sim.cpp:29987-29992), the plan goes straight to REGROUP with `retreat = base_x/z`. That skips the ABORT pick of the rally point or the nearest base on the army's land.
- **Effect**: on naval maps, the survivors of a failed overseas attack are locked for up to 3 minutes and use ships for a trip 0.28 never makes. In 0.28 they would join the next plan where they stand. On land, an army whose target vanished walks all the way to the primary base instead of to the border rally point or a captured or forward base.
- **Save change**: no.
- **Fix sketch**: in ABORT, start `home_x/z` unset. If neither the staging point nor a base is on the army's land, issue a stop, call `petra_out_of_plan` for each unit and go to UNEXECUTED with no ferry. Route the arrival-no-target case at 30530-30536 through `AttackPlan::ABORT` instead of REGROUP.

## P4. A target switch re-orders the whole army; 0.28 only changes the target (LOW-MEDIUM)
- **0.28** UpdateTarget (attackPlan.js:1981-2068) only sets `this.target / targetPos / targetPlayer` and issues no orders. Units move on through the per-unit loop (attackPlan.js:1543-1600): only idle units, units whose target is gone or now allied, units on over-targeted (vetoed) unit targets, and a few "maybe" cases get a new order, 10 units a turn. Units in a fight keep fighting. The "help another attack" branch also takes `this.targetPlayer = attack.targetPlayer` (attackPlan.js:2045).
- **RTS0** ATTACKED retarget (sim.cpp:30636-30642) calls `order_attack_move(squad...)` on every squad member whenever the target changes. The help branch (sim.cpp:30631-30632) keeps `plan.target_player` set to the old enemy.
- **Effect**: each time a target falls (often several times per attack), every soldier drops its current fight, including ones locked on a gate, a siege engine or a hero picked by the per-unit scoring, and re-acquires targets on the attack-move. Siege engines leave the structure they were shelling. Units already in melee mostly pick the same foe again, but ranged units and siege lose time, and the per-unit vetoes are reset. A5's decision (attack-move at *arrival*) does not cover this mid-fight case.
- **Save change**: no.
- **Fix sketch**: on retarget, update only `target_building_id/x/z` (and `target_player` from the new target's owner, for the help branch too). Order idle squad members only. petra_attack_units already re-picks idle and invalid-target units on the next turns.

## P5. Overseas preparation: musters and trains with no ship; asks for the full fleet early; no completing step; rally base always the primary (LOW, naval maps only)
- **0.28**:
  - The constructor (attackPlan.js:76-108) sets `overseas` when no base shares land with an enemy structure, and asks for 1 transport ship. updatePreparation returns KEEP_GOING *before* assignUnits and trainMoreUnits while that sea has no transport ship (attackPlan.js:479-480). So 0.28 does not build the army until a ship exists.
  - `neededShips` (3 for an Attack, 5 for a HugeAttack, times popScaling) is asked for only in chooseTarget at mustStart (attackPlan.js:895).
  - With no base on the target's land, the rally base is the one minimising distance / sqrt(island size) (attackPlan.js:868-881), not simply the first base.
  - The completing step also runs for overseas plans. Units return their loads and gather at the rally base for up to 60 s (attackPlan.js:591-611), then StartAttack requires transport for the gathered army (attackPlan.js:1311-1319).
- **RTS0**:
  - MUSTER assigns and trains with no ship check. The ship check (sim.cpp:30251-30256) runs only once the plan is ready.
  - `plan_wants` (sim.cpp:26905-26909) asks for 3 or 5 ships through all of MUSTER.
  - Overseas plans skip completing. `petra_start_transport(squad, base_x, base_z, ...)` boards the units from wherever they are working (sim.cpp:30349-30359), with no load drop and no gathering.
  - The rally base is always `get_primary_petra_base()`.
- **Effect**: on island maps, RTS0 spends on the army before any ship, then builds the full transport fleet earlier. Drafted citizen soldiers sail with their loads.
- **Save change**: no.
- **Fix sketch**: in MUSTER, when `petra_plan_overseas(..., provisional target, true)` is non-zero and `petra_transport_ships_on(sea) == 0`, return before assignUnits and training (train_for gate at sim.cpp:22718 too). In plan_wants, ask for 1 ship during MUSTER and `need_ships(kind)` only once completing or WALKING. Give overseas plans the same completing block as land plans (`order_return_resource`, then walk to the base anchor), and board when completing ends.

## P6. A rush fixes its enemy when the plan is made; a forced rush never falls back to defaultTargetFinder (LOW)
- **0.28** A rush's `targetPlayer` stays undefined until chooseTarget at mustStart (attackPlan.js:828-833). It is then picked by getEnemyPlayer(rush), with the ">6 towers/fortresses" veto and currentEnemyPlayer read at that moment, and currentEnemyPlayer is set then. getNearestTarget for a Rush falls back to defaultTargetFinder `if (hasSiegeUnits() || forced)` (attackPlan.js:932-936), e.g. after an ally's attack request forces it.
- **RTS0** The UNEXECUTED rush branch (sim.cpp:30016-30020) calls `find_petra_rush_target(..., plan.target_player)`. That picks the enemy and calls `petra_note_enemy` at creation (sim.cpp:29391), then stores `plan.target_player`. At must-start, `petra_plan_next_target` (sim.cpp:29976-29980) passes that stored player as `prefer`, so the veto and the current enemy are not re-read. `find_petra_rush_target` always calls `petra_nearest_target(RUSH, false, ...)`, which returns after the undefended-structure pass for a rush (sim.cpp:29091).
- **Effect**: an enemy who raises a seventh tower during the rush's muster is still rushed. currentEnemyPlayer changes at plan creation, not at launch. A rush forced by an ally's request with no undefended structure aborts instead of hitting a defended one.
- **Save change**: no.
- **Fix sketch**: after the provisional pick at creation, reset `plan.target_player = 0xFF` (as the Attack branch does at sim.cpp:30054) and skip `petra_note_enemy` there. At must-start, pass `plan.forced` through to `petra_nearest_target`, and allow its default-finder fallback for a forced rush.

## P7. A landed overseas army waits for an 18 s / 48 m gathering and does not answer attacks while ferrying (LOW, naval maps)
- **0.28** UpdateTransporting (attackPlan.js:1823-1862): the moment no unit of the plan is in transport, the state is STATE_ARRIVED and the full startingAttack pass runs. While part of the army is still at sea, every idle landed unit attacks whoever hits a squad member.
- **RTS0** WALKING with no path (sim.cpp:30378-30410, 30427-30431) `return`s while a ferry runs, with no retaliation (only native UnitAI reacts). Once all units have landed, it waits until the army is within 12 units (48 m) of the drop point, or 18 s, or more than one attacked unit, before it attacks.
- **Effect**: landed waves stand on the beach without answering hits as a group. The attack starts a few seconds later than in 0.28.
- **Save change**: no.
- **Fix sketch**: while units of this plan are still aboard (P2 test), run `petra_answer_attackers` on the landed members. Set `arrived = true` as soon as none are aboard.

---

## Checked and matching (beyond A1-A18)
- getEnemyPlayer: the wonder/relic coin flip, Gaia relics only through currentEnemyPlayer, the rush tower veto, currentEnemyPlayer used only while `targetPlayer` is unset, nearest same-land CC, strongest +500 (attackManager.js:506-677 / sim.cpp:28935-29019).
- getNearestTarget/rushTargetFinder: undefended means no defensive-fire structure of that player within 80 m; the decay rule in isValidTarget; a Rush falls back to any enemy; an Attack goes to the default finder only with siege or forced; the defaultTargetFinder victory targets pooled before the CC/ConquestCritical/any/units tiers; fields +100000 (attackPlan.js:899-1093 / sim.cpp:29052-29136, 29386-29396).
- chooseTarget: a second getEnemyPlayer when the first finds nothing; a rally base on the target's land; the uniqueTarget plan stops when its target dies (attackPlan.js:826-905 / sim.cpp:29183-29187, 30262-30272, 29975).
- Completing step for land plans: target re-check, maxCompletingTime 40/60 (rush or forced), loads returned first, transport for units on another land, ally attack request posted (attackPlan.js:455-611 / sim.cpp:30080-30332). The only difference is P2's transport scope.
- mustStart/canStart/capped/forceStart/Fasten-the-end-game; per-class counting is first-match in RTS0 against all-match in 0.28, but the 0.28 class sets are disjoint, so the counts are the same.
- UpdateWalking (attackedNB rules, pos5 stuck-siege delete, wall stuck test, 100 m arrival, 40 m waypoint) (attackPlan.js:1864-1979 / sim.cpp:30433-30520).
- UpdateTarget order: unit target followed or its holder taken, same-land retarget, help another attack, another enemy, else abort; target captured by an ally or by us changes the target, one captured by another enemy stays (checkEvents attackPlan.js:2136-2160) (sim.cpp:30583-30648).
- update() has no strength-based retreat in either tree. A started attack ends only when its army dies, no target is found, walls stop it, or cancelAttacksAgainstPlayer / a relic abort applies (attackManager.js:337-353, 680-697 / sim.cpp:30646, 34651, 34981-34993).
- switchDefenseToAttack: CC captured, range 150 on the foes' position; disbanding army, armyID + uniqueTarget, defender within 120 m on the target's land (attackManager.js:739-797, defenseManager.js:540-556, 940-952 / sim.cpp:30749-30782, 33577-33608). Small gap: the CC retake doesn't check that defenders are on the target's land (isAvailableUnit/accessOk). Not listed separately.
- Stances: default stances already match 0.28 StartAttack (aggressive; packable siege stand ground).
- allowCapture (entityExtend.js:160-208 / sim.cpp:29427-29470): same thresholds. RTS0 has no Capture bonuses (getAttackBonus = 1), as documented.
- Attack requests (attackManager.js:68-116 / sim.cpp:31695-31750).
