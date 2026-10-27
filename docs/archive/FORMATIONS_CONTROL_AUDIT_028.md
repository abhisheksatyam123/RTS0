# Formation controllers against 0.28 (Formations pass)

Static comparison of `src/simulation/simulation.cpp` formation controllers with 0 A.D. 0.28
`simulation/components/Formation.js`, `UnitAI.js` (FORMATIONCONTROLLER states) and
`simulation/helpers/Commands.js` (GetFormationUnitAIs). Tracked as REMAINING_GAPS G63.

## Fixed

1. **Attacking an enemy formation (medium).** 0.28's Order.Attack on a formation member targets
   that member's controller (UnitAI.js 774-779); COMBAT.ATTACKING enter has each member pick the
   enemy member closest to itself (`GetClosestMember(this.entity)`, 2161-2168), and the
   controller's approach and range check use the member closest to the controller (4811-4816,
   4904-4909). RTS0 had every member pile onto the clicked soldier. Now
   `FormationController::target_formation` (save v120) holds the enemy formation,
   `formation_attack_target` / `formation_closest_member` stand in for GetClosestMember, and
   `formation_members_attack` spreads the members. Once the first soldier falls each member's
   own FINDINGNEWTARGET takes over; 0.28's re-pick there passes the attacker, not the member, to
   its filter (`filter(ent)`, Formation.js 224), so it finds no one and the unit goes on to
   FindNewTargets, which RTS0's existing target search already does.
2. **New formation facing.** MoveToMembersCenter turns a new controller to the plain mean of the
   members' Position rotations (`rotations / positions.length`); RTS0 used their velocity and
   faced +z when they stood still. Now the mean of their headings, as 0.28 averages it.
3. **Turn check.** MoveMembersIntoFormation compares the new heading with the controller's
   current rotation (`DoesAngleDifferenceAllowTurning(newRotation, oldRotation)`); RTS0 compared
   it with the heading the slots were assigned for, so a walk that had turned gradually
   reassigned every slot on the next order.
4. **Rearrange.** FORMATIONCONTROLLER IDLE and MEMBER call `SetRearrange(false)`, so RemoveMembers
   doesn't move the rest; RTS0 re-formed on every death, pulling fighters out of a melee. Now
   only walking, approaching and patrolling formations re-form at once; an idle one re-forms on
   its 2 s IDLE timer as before.
5. **Split selections.** Commands.js GetFormationUnitAIs takes members picked out of a formation
   out of it and forms them up on their own for the command. RTS0 did that for walks, attacks
   and patrols, but a gather or repair on part of a formation sent the picked members one by
   one. A player's (forced) gather or repair now goes through the same path.

## Not ported

- **Stuck controller (veryObstructed).** 0.28's controller is an obstructed UnitMotion entity
  with the largest member's clearance and can get stuck, so WALKING sends the members on their
  own for 3 s. RTS0's controller is a point walking its long-range path without obstruction, so
  it can't be stuck that way; nothing to port.
- **"combat" walk variant.** 0.28 sets the members' `combat` animation variant on an
  attack-walk and approach (presentation; low). Open in G63.
- Heal and garrison on part of a formation still act member by member (internal orders call
  the same functions); open in G63 as a nit.
