# Petra attacks, pass 3: the fighting phase (Formations)

Static comparison of 0 A.D. 0.28 `attackPlan.js` update (an arrived army: Attacked events, unit and structure picks, UpdateTarget) and `entityExtend.js` allowCapture against `petra_attack_units`, `petra_update_attack_plan` (ATTACKED) and `petra_allow_capture`. Passes 1 and 2 (`FORMATIONS_ATTACK_AUDIT_028.md`, `FORMATIONS_ATTACK_AUDIT_028_PASS2.md`) are not repeated; their veto weights, scoring, ranges, the 10-units-a-turn loop and the UpdateTarget order were re-checked and match.

## Fixed (no save change)

- **F1 allowCapture threshold** (entityExtend.js:206): `garrisoned()` returns the garrison array, true in JS even when empty, so every shooting garrison holder (towers, fortresses, civic centres) needs points/50, garrisoned or not. RTS0 used /80 for an empty one and tried to capture with too little.
- **F2 UpdateTarget first** (attackPlan.js:1375): the per-unit loop now runs after the target switch, toward the new target, and the switch no longer re-runs the all-units arrival pass on the next turn.
- **F3 helpers for a hit unit** (attackPlan.js:1397, 1430, 1444): filterNearest includes the hit unit, so an ordinary attacker draws one helper (not two), a siege attacker five melee units counting the hit one, and units of class Siege (war elephants are isSiegeUnit but not Siege) take places around a hit siege engine.
- **F4 help the nearest fight** (attackPlan.js:1790-1805): a unit in combat on a structure counts too.
- **F5 stuck approaching** (attackPlan.js:1736-1742): a unit held up approaching its target attack-moves with no avoid list (walls).
- **F6 random pick** (attackPlan.js:1665, 1726, 1779): randIntExclusive(0, n * share) is floor(random * n * share), so the second-best of 11 foes has a 1/11 chance, not 1/2.
- **F7 next target's land** (attackPlan.js:2220-2223): the land of the unit nearest the army's centre, not of the first squad member.
- **F8 no per-turn capture check**: RTS0's `petra_check_captures` (squad-only, capture to damage only, each turn) has no 0.28 counterpart; capture and damage switch only in headquarters.js updateCaptureStrength (`petra_update_capture_strength`). Removed.

## Fixed with a save change (O1, save 131)

- **O1 siege attack-moves at anything** (attackPlan.js:1531-1534, 1669-1678): a siege engine or Elephant+Melee with no structure in reach attack-moves with `attack: ["Structure"]` (melee siege also avoids ships), so it walks past soldiers. RTS0's attack-move has no structures-only class, and the saved order flag byte is full, so this needs a save change. Medium. **Fixed** (save 131): `order_attack_move(..., structures_only)` marks the unit in `attack_move_structures_` (sorted ids, saved), the walk-and-fight scan then skips units and animals, and `set_order` drops the mark on any other order. Petra's siege uses it when the plan's target is a structure.
- **O2 canAttackTarget**: 0.28 filters unit and structure candidates by `canAttackTarget`; RTS0 does not. Not confirmed whether any RTS0 unit is barred from a candidate there. Nit until shown.
- Nits kept: with isBlocked, 0.28's structure pick allows only the plan target; allowCapture is true for an invulnerable target.
