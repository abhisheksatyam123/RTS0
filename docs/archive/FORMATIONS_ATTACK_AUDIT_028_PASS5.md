# Petra attacks, pass 5: the launched attack (Formations)

Audit of 2026-11-02 against main ce0c9db2d, tracked as G104 in `docs/REMAINING_GAPS.md`. Sources: 0.28 `attackPlan.js` (AP) `update`, `UpdateWalking`, `UpdateTarget`, `getNearestTarget`, `checkTargetObstruction`, `Abort`, `getAttackAccess`. Passes 1-4 and G42/G75/G94/G97 are not repeated.

## Fixed (no save change)

- **L1 walled-in targets are targets** (AP:1035-1041, terrain-analysis accessibility). RTS0 decided whether a target was reachable from nav regions, which walls and palisades split, so a structure inside a closed wall was never picked by an attack or a rush. 0.28's land access ignores structures; the wall piece in the way is then taken by checkTargetObstruction. `petra_nearest_target` now also accepts a target on the search point's terrain access (`petra_access_at`). Medium.
- **L2 rushes run checkTargetObstruction** (AP:912-976, 1118-1222). `find_petra_rush_target` skipped the blocker check that `find_petra_attack_target` runs, so a rush on a palisaded base marched to a house behind the palisade and stalled outside, its attack-move avoiding walls. The blocker becomes the target and its owner the enemy, at must-start and at arrival. Medium.
- **L3 arrival runs UpdateTarget at once** (AP:1351-1369, 1981-2068). A target lost during the march was replaced on arrival from the primary base's reach (sea crossings included) with no help-another-attack step, and the plan aborted when that found nothing. Arrival now takes the land of the unit nearest the army's centre and helps another started attack first, as the fight does (`petra_attack_retarget`). Low-medium.
- **L4 every unit of a big army gets its "maybe" re-picks** (AP:1594-1601). With 15+ units the rolling 10-a-turn window and the per-id 3/6-turn cadence could never meet for some units (gcd of the two > 1), so a siege engine on a unit or a slow soldier on a fleeing woman was never re-targeted. The cadence now counts the unit's window visits (`petra_attack_maybe_due`). Low-medium.
- **L5 Abort takes getAttackAccess** (AP:2075-2113, 2218-2224). `petra_abort_plan` took the land under the army's centroid, which can be water or a cliff for an army spread round a lake, and then released the army where it stood. It uses the land of the unit nearest the centre (`petra_attack_access`). Low.

## Open

- **L6 isBlocked** (AP:915, 1216-1220; low-medium, save): 0.28 sets it when checkTargetObstruction substitutes any blocker (a house, a tower, a gate) and clears it on every new target; RTS0's `petra_plan_blocked` tests whether the target is a wall piece. Needs a saved flag on AttackPlan.
- Nits: a 0.28 rush takes another enemy's undefended structure before helping another attack (AP:1093); the wall-stuck check ignores Gaia walls (AP:1922 counts them); a target with no path at all is dropped in 0.28 (AP:1128); a rush's arrival pick isn't limited to the army's land in 0.28 (AP:1358).

## Checked and matching

Attacked reactions and helpers; veto counts and unit scoring; pick ranges (60 / 30+range / 90, own range when blocked); siege and non-siege structure picks; UpdateWalking arrival tests, stuck siege and wall checks, waypoints; resigned enemies; help-attack filters; unit targets and their holders; Abort's rally point and nearest base.
