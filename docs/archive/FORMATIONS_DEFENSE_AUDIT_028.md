# Formations pass: Petra 0.28 defense vs RTS0

Audit of 2026-10-15 against main 00f2209b2, tracked as G35 in `docs/REMAINING_GAPS.md`. Fixed so far: D1, D2, D3.

Sources: 0.28 `defenseManager.js` (DM), `defenseArmy.js` (DA), `garrisonManager.js` (GM), `headquarters.js` (HQ), `attackManager.js`, `baseManager.js`, `emergencyManager.js`, `entityExtend.js` (EE). RTS0: `src/simulation/simulation.cpp` at main 00f2209b2 (line numbers are from that tree). I left out everything in audit-028 H1-H4, M1-M9 and L1-L11 (L7, the defender pick order, is still open there and is not repeated here), the re-check 3 table, and scale conversions.

Militia: neither tree has anything by that name. The closest things are the emergency training and the base `neededDefenders`, and both match (see the matches list at the end).

## Differences

**D1. (Fixed.) Petra's own retaliation for attacked units outside armies and plans is missing.**
- 0.28 (DM:695-754): an own soldier (not Support, not in a started plan, not in an army) hit by an enemy Unit gets `target.attack(attacker)`. The exceptions are a unit already in COMBAT against the attacker or against a non-structure, non-support target, and a unit repairing a structure that shoots. A soldier capturing a structure also pulls the nearest squad-mate on the same structure (`capturableTargets`) onto the attacker.
- RTS0: nothing in Petra does this. The Attacked events (`petra_turn_attacks_`, simulation.cpp:28435-28457) only feed `add_attacker` and the fishing-boat flight. Only the native UnitAI answers (`respond_to_attacker`, simulation.cpp:16727-16735). That ignores attackers while the unit is on a forced order (walking to gather or build: Petra's `order_gather` defaults `force=true`, simulation.h:783). It also leaves a unit hitting a structure or a support unit on its target when the attacker is ranged.
- Effect: Petra soldiers walking to work under fire from cavalry archers keep walking until they start gathering. Soldiers hacking a structure ignore ranged attackers.
- Impact: medium-low. Save: no.
- Fix: in `petra_manage_defense`, for each `petra_turn_attacks_` pair where target and attacker are units, the target is our soldier, not in a defense army and not in an attack squad, apply DM:698-711 and `order_attack` the attacker. Add the capturableTargets helper.

**D2. (Fixed.) Defenders are re-ordered every turn, and an attacking defender is walked home.**
- 0.28 (DA:575-582): `assignUnit` runs only for a defender with no orders and no transport. A defender that UnitAI moved onto another target keeps it: the next enemy after a kill, a tower that shot it, a unit not in the army. When no foe qualifies (all past the 5/8 cap), `assignUnit` returns false and the unit is left alone (DA:356-357).
- RTS0: any defender not in ACTION_ATTACK on a foe of its own army gets a new order every turn (simulation.cpp:28806). That includes defenders fighting a structure or a non-army unit. With no assignable foe, a defender in ACTION_ATTACK is walked to the primary base (simulation.cpp:28843-28845). That has no 0.28 counterpart and can pull a unit out of a melee.
- Impact: medium-low. Save: no.
- Fix: re-assign only when the defender is idle (no order, not a passenger), and drop the walk-home branch.

**D3. (Fixed.) Defenders don't drop off their load first.**
- 0.28 (DA:309, EE:229-262): `returnResources` sends a defender carrying resources to the nearest own dropsite on its land, with the attack queued after.
- RTS0 (simulation.cpp:28842): `order_attack` straight away.
- Effect: RTS0's citizen soldiers reach the fight sooner than 0.28's but bank their loads later.
- Impact: low-medium. Save: no.
- Fix: in the assignUnit block, if `carry>0` and a dropsite exists on the same land, order the return and queue the attack.

**D4. Enemy units are scanned every turn instead of one player per turn.**
- 0.28 (DM:208-239): `checkEnemyUnits` looks only at player `playedTurn % nbPlayers`, Gaia included. In a 1v1 that is each enemy (and Gaia's animals) once every 3 turns, about 4.8 s.
- RTS0: the enemy-unit scan (simulation.cpp:28578-28603), the animal scan (28467-28490) and the targetList scan (28608-28621) run every turn for every enemy. Only the own/Gaia capture checks use the rotation (`whose`, 28627).
- Effect: walk-ins with no attack become armies up to about 3 s sooner. The Attacked path runs every turn in both, so hits are answered at the same speed.
- Impact: low. Save: no.
- Fix: run those scans only for `whose == team + 1` (Gaia's animals on `whose == 0`).

**D5. Armies form on intent, not only on hits.**
- 0.28: an army comes from an Attacked event (DM:636-642) or from isDangerous.
- RTS0 adds two triggers 0.28 doesn't have:
  - Any foe in ACTION_ATTACK whose target is our unit joins an army from neutral or allied land at any distance, before it has hit anything (simulation.cpp:28459-28465; also 28585-28588 in our land).
  - A foe within the radius of a base without an anchor (simulation.cpp:28593-28601).
- RTS0 also makes armies out of whoever hits our ships (28435-28456 only skip fishing boats and attacker ships). 0.28 skips any Ship target (DM:603).
- Impact: low. Save: no.
- Fix: drop the targeting loop and the base-radius rule. In the event loop, skip targets that are ships.

**D6. Warships are never defenders.**
- 0.28 (DM:430-455): candidates exclude only Support, StoneThrower, FishingBoat and units in transport. Warships qualify, and `assignUnit` lets a Ship attack across access (DA:363).
- RTS0 (simulation.cpp:28708): excludes TC_SHIP.
- Impact: low (none on land maps). Save: no.
- Fix: allow non-fishing, non-transporting warships and skip the land/ferry test for them, as the code already does at 28829.

**D7. The backup rule doesn't count units with no reachable army.**
- 0.28 (DM:493-498): while `backup < 12`, a candidate with no army at all also goes to backup and is dropped from both passes. That is a unit on another land in pass 0, or one that can't attack any army's foes.
- RTS0 (simulation.cpp:28750): `if (best < 0) continue;` without counting it, so the unit is still offered in pass 1.
- Effect: on island maps or against capture armies, RTS0 commits up to 12 more units than 0.28.
- Impact: low. Save: no.
- Fix: before `continue`, if `backup < 12`, increment `backup` and mark the candidate used.

**D8. Breakaway timing.**
- 0.28 (DA:612; DM:421 and DM:328): the breakaway check runs when `elapsed - positionLastUpdate > 5`. `assignDefenders` calls `recalculatePosition` every turn for each army that still needs defenders, and the every-5th-turn danger check does the same for all armies. So an under-defended army never sheds breakaways, and a satisfied one does every 5-8 s.
- RTS0 (simulation.cpp:28205, 28514-28528): every army is checked on a global 5 s tick.
- Effect: RTS0 splits large, spread-out invasions into more armies.
- Impact: low. Save: no.
- Fix: skip the breakaway check for armies whose `need > 0` (computed the turn before, or this turn before the check).

**D9. Tower fire doesn't abort a capturing army.**
- 0.28 (DM:644-669): a capturing army is aborted when any non-ally attacker hits one of its units, structures included. In 0.28 a shooting Gaia tower being captured knocks its capturers off every turn, and the army re-forms on Gaia's turn.
- RTS0 (simulation.cpp:28498-28503): only an enemy unit hit counts (`lookup_unit`).
- Impact: low. Save: no.
- Fix: also accept a structure attacker from `petra_attacker_of`.

**D10. Sheltering support units and stone throwers keys on targeting, not hits.**
- 0.28 (DM:673-687): runs on Attacked events, so tower arrows count too. It also skips units in started plans only.
- RTS0 (`petra_manage_garrisons`, simulation.cpp:28050-28069): runs for foe units whose `target_unit_id` is ours. So a woman merely chased below 55% health shelters early, and one shot by a tower never does. It also skips all reserved units, mustering ones included.
- Impact: low. Save: no.
- Fix: iterate `petra_turn_attacks_` (any attacker that isn't an ally) instead of foe targets, and use `in_attack_squad`.

**D11. targetList membership.**
- 0.28 (DM:35-40, DM:104-131): a structure enters the list when a builder seen repairing it qualifies, and it stays until it dies or stops being an enemy's. An enemy CC that finishes stays a target. The CC distance test is from the builder, against own structures with any foundation progress.
- RTS0 (`petra_defense_target`, simulation.cpp:24838-24849): recomputed from scratch. A forward CC drops out as soon as it is finished (24841). The distance is from the CC to our *finished* structures only (24843).
- Effect: after completion, `switch_to_attack` and the isDangerous radius ignore it.
- Impact: low. Save: yes for an exact list (or no save: also accept finished enemy CCs within 30000 m² of our structures).
- Fix: keep finished CCs in `petra_defense_target` and accept foundations with progress > 0 as the own reference.

## Matches (checked in both)
- Defense ratios own/ally/neutral and the ally split (DA:416-441; simulation.cpp:28677-28692).
- Compact/breakaway/merge sizes. Capturing armies are never merged.
- Disband on emptiness, and `switchToAttack` (same land, 120 m, unique target, Sandbox off).
- Disband in enemy land, and in neutral land beyond the CC 200 m / dock 100 m reach on every 5th turn.
- `clear` destination rules and the low-health heal garrison.
- Capturing armies: own structure with at least 25% lost on our own turn and no armies; Gaia capturable or shooting structures in our land on Gaia's turn with at most 1 army; strengths 2 / 6×arrows / 4; disband when no enemy points remain; allowCapture switch to damage.
- needsDefenders strength from max-health `getMaxStrength`, elephants ×3.
- Backup of 12 beyond 200 m outside our land.
- assignUnit foe choice (the foe hitting it first, then ≤2, ≤5, or ≤8 for hero/siege), and the defense ferry request for another land.
- Fishing boats back off (range + 15 m / 25 m).
- Ships and traders skipped in `checkEnemyUnits`; animals in COMBAT on ours or an ally's count.
- isDangerous own-structure reach (6000 m², ranged range + 30 m), the blinking/defendable test, ally CC cooperation, and `attackedAllies`.
- `trainEmergencyUnits`: nearest anchor on the same land, 5 s queue cut, 20000 m² distcut, stopProduction, random ranged/melee with autogarrison, citizen infantry only, emergency account transfers.
- baseManager `neededDefenders` 3 + 2(d-3) from Medium, trained after the CC finishes.
- `garrisonUnitsInside`: arrow classes, melee rule, plan/army pulls, same land, nearest first, eject-health check, decay minimum.
- `keepGarrisoned` PROTECTION/EMERGENCY rules.
- `garrisonAttackedUnit` with the emergency unload, and `garrisonSiegeUnit`.
- emergencyManager thresholds and references.
- isNearInvadingArmy 12000 m².
- CC-captured retake (range 150).
