# B audit pass 6: diplomacy and formation control in depth, against 0 A.D. 0.28

Area: the diplomacy simulation (Player.js, Diplomacy.js, Commands.js diplomacy, tribute, spy request and attack request; VisionSharing, GarrisonHolder and Market reactions), and formation control (Formation.js, UnitAI FORMATIONCONTROLLER and FORMATIONMEMBER, the Commands.js "formation" command).

The diplomacy simulation matches 0.28 throughout:
- stances, mutual worsening and mutual allies
- Allied View
- what a stance change triggers: garrison eject, gates, auras, guard charges, territory decay, BuildingAI and attack checks, trade routes
- garrison vision
- spy cost, duration and bribable units
- tribute
- ceasefire and locked-team refusals

## Differences

1. **An idle formation never re-gathered after its members fought on their own.**
   - 0.28: the IDLE timer (UnitAI.js:998-1027, every 2000 ms) calls MoveMembersIntoFormation once every member is idle.
   - Medium.
2. **Changing formation stopped a marching or fighting formation.**
   - 0.28: Commands.js:608-613 calls MoveIntoFormation, which is PushOrderFront (UnitAI.js:5249-5260). FORMING re-forms in place, then resumes the order.
   - Medium.
3. **A casualty sent every idle or walking member back to its slot, even mid-fight.**
   - 0.28: IDLE and MEMBER call SetRearrange(false), so Formation.js:425-426 doesn't rearrange.
   - Medium-low.
4. **Members are spaced by collision radius, not their Footprint.**
   - 0.28: Formation.js:624-653 uses the Footprint: cavalry 3x6 m, elephants 4.5x9 m, rams 8x12 m.
   - Medium-low. Needs a footprint table; left open.
5. **The column switch measured the nav path of the current leg.**
   - 0.28: ComputeWalkingDistance (UnitAI.js:5317-5337) sums straight lines through every queued walk or patrol point, and stops at the first order with a target.
   - Low-medium.
6. **Leaving a team renumbers teams.**
   - Already known: pass 3 item 9 and the session audits.
7. **A formation garrisons by walking in shape to the holder.**
   - 0.28: the controller has no Garrisonable range, so the members walk one by one.
   - Low (only with formationwalkonly off). Left open.

## Status (2026-10-20)

- 1, 2, 3 and 5 are fixed in the pass-6 PR. test_formations 4b checks the march carrying on and the idle re-gather; both fail without the fix.
- 4 and 7 are REMAINING_GAPS G55.
