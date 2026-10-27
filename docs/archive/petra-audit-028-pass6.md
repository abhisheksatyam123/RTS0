# Petra garrisons, workers, queues and HQ economy: sixth pass against 0.28 (area C)

This is a read-only audit of RTS0's Petra port against 0 A.D. 0.28 Petra. It covers:

- `garrisonManager.js`
- `worker.js`
- `queueManager.js`, `queue.js` and `queueplan*.js`
- the economy parts of `headquarters.js`: houses, phase requirements, farmstead, corral, market, forge and temple
- `researchManager.js`
- `chatHelper.js`

Rows already listed in pass 4, pass 5, the Compare pass-6 Petra audit or REMAINING_GAPS are left out. Those rows are G1-G5, Q1-Q7, W1-W5, P1, R7, B1-B10, T1-T11, H1-H7 and pass-6 #1-#5k.

The following deliberate differences are kept and not reported:

- the queue funds an item a turn late (Q7, pass-6 #3)
- the wood-starved clause
- the 4 m scale: distances /16, ranges x0.28, melee x0.6, build times x0.25

The IDs carry on from the earlier passes: GM for garrisons, W6 and up for workers, Q8 and up for queues, H8 and up for headquarters. All `simulation.cpp` line numbers are from the current main.

| ID | Area | Impact | Save | 0.28 | RTS0 | Summary | Fix sketch |
|----|------|--------|------|------|------|---------|-----------|
| Q8 | Gather split | Low (uncertain size) | no | queueManager.js:86-158 (wantedGatherRates), queueplanBuilding.js:901-925 (houseNeeded isGo) | simulation.cpp:23040-23060 (`want_rate` = shortfall/30), 22789-22825 (`petra_resource_demand`) | 0.28 builds the wanted rates from three terms after taking away the stock: each queue's first isGo plan /30, its second isGo plan /60 ("medium"), and the first plan that is not isGo /300 ("long"). RTS0 counts one want per queue /30 and drops both other terms. In practice RTS0 never counts the second queued worker TrainingPlan (food), and never counts the house plan that waits on houseNeeded (wood), so food and wood are asked for less than in 0.28. A plan queue's 60% of the next batch is counted at /30 instead of the full second batch at /60. | Keep a short/medium/long total like 0.28. Short: what is in now. Medium: the second `petra_worker_plans_` entry of each queue, at its batch cost. Long: one house cost when `petra_kept_.house_plans > 0` and the house is not yet needed. Take the stock off short, then medium, then long, and divide by 30, 60 and 300. Medium and long default to 0 (config.js:158-163). Both lists are already saved (v113/v114). |
| H8 | Corral | Low | no | headquarters.js:1408-1421 (the livestock TrainingPlan goes in `queues.corral`, priority 1 at config.js:126); queueManager.js:371-394 | simulation.cpp:24331-24343 → `cmd_breed_livestock` (10319-10331) pays from `player().pay` | Petra's livestock breeding skips the corral queue and pays straight from the stock. So it can spend food that other queues have saved, for example for the phase or for workers, and distributeResources then has to scale the accounts back down. In 0.28 the animal waits in the corral queue at priority 1 and is paid only from resources no other queue holds. | Before `cmd_breed_livestock`, call `petra_try_pay(PETRA_Q_CORRAL, food, wood, gold, stone)` with the animal's `fauna_train_cost` (at the corral's civ), as `petra_train_template` does. Then breed without paying twice, either with a pay=false flag or by refunding the stock. |
| H9 | Corral | Low | no | baseManager.js:470-485 (a corral laid because fields can't be built sets `HQ.needCorral = true`); headquarters.js:2292 (manageCorral only while needCorral); headquarters.js:1386-1387 (a second corral tests `isTemplateAvailable(field)`) | simulation.cpp:24313-24328 | **Part 1.** While fields can't be built (`can_build(BLD_FIELD)` false, usually the 90 s no-room mark), RTS0 lays the first corral but never sets `petra_kept_.need_corral`. Once fields can be placed again, line 24315 returns early, so that corral never breeds and is never counted for checkPausedQueues. In 0.28 needCorral stays set for good after this. **Part 2.** The second-corral test uses `can_build(BLD_FIELD)`, which includes the no-room mark. 0.28 uses isTemplateAvailable, so a field with no room makes RTS0 lay extra corrals (up to the phase number). | **Part 1:** set `petra_kept_.need_corral = true` when the corral in the `corrals == 0` block is placed while `!can_build(BLD_FIELD)` and the base's food is under 900. **Part 2:** test the field's availability without `petra_no_room` (civ, phase, trigger and limit only). `need_corral` is already saved. |
| W6 | Gatherers | Low | no | worker.js:336-356 (RETURNINGRESOURCE: for a "sparse" resource, stone or metal, the supply mark is cleared when the supply is not in the base's nearby list while that list has entries, or not in medium while medium has entries) | simulation.cpp:23166-23182 (recheck only when `petra_supply_of != target`), 23278-23304 | A stone or metal gatherer sent to a medium or faraway mine keeps its supply mark for good, so the recheck at worker.js:242 never runs for it. A storehouse later built near another mine does not draw it back. In 0.28 every trip back to a dropsite clears the mark, and on its next GATHER turn startGathering moves it to a nearby mine. | In the recheck pass, take our units in `ACTION_RETURN_DROPSITE` carrying stone or gold whose `petra_supply_of` is not in their base's `nearby` list while that list is non-empty (or not in `medium` while `medium` is non-empty), and call `petra_set_supply(uid, 0)`. The existing recheck then runs when they go back to gathering. The supplies list must be built when either condition holds. `petra_supply_` is already saved (v94). |
| W7 | Idle carriers | Low | no | worker.js:213-227: an idle gatherer carrying the same type as its gather-type goes straight to startGathering; returnResources only for another type | simulation.cpp:22975-22999 | Every idle Petra worker carrying anything walks to a dropsite first, and is reassigned only once it is empty, on its base's next turn. A gatherer whose supply ran out carrying 5 wood therefore makes an extra round trip and waits a turn before it is sent to the next wood. | Pick the gather type first: the W1 rule, `res_type_carried`. Return first only when the carried type differs from the chosen type, or when there is no supply for it; otherwise call `order_gather` with the load kept. |
| W8 | Transport | Low (naval maps) | no | worker.js:44-87: while BOARDING, a builder whose target foundation is gone leaves the plan, and so does a gatherer or hunter when the destination land has no structure or support unit of ours taking its gather-type | simulation.cpp:31351-31420 (`PetraTransportPlan::BOARDING`): no such check | Workers waiting at the beach for a PETRA_TRANSPORT_WORKERS or _BUILDERS crossing stay in the plan after their reason is gone, so the ship still carries them over. | In BOARDING, for worker and builder plans, drop a passenger that is not yet aboard in two cases. First, a builder plan whose foundation no longer exists. Second, the destination land has no dropsite for the passenger's `res_type_carried`, or for food when it carries nothing. Use `petra_mobile_dropsite_on_land`-style tests on the drop land. Then `cmd_stop` the passenger. |
| GM1 | Garrisons | Low | no (approximation) / save (exact) | garrisonManager.js:325-326 and 364-369; defenseManager.js:779 and 834 (garrisonType decided per unit: TYPE_DECAY only for the gmin units garrisonUnitsInside sends, TYPE_PROTECTION for the rest) | simulation.cpp:32269 | While a shooting structure, or a just-captured barracks, decays, RTS0 keeps every capture-capable unit of ours inside it. Units that came in for protection, such as soldiers sheltering from an attack, therefore never leave once the enemy goes. 0.28 judges those by the protection rules and keeps only the gmin units sent for the decay. | Approximation: keep at most `decay_min` capture-capable units by the decay rule, in garrison order, and run the rest through `petra_keep_garrisoned`. Exact: a per-unit garrison-type byte, which needs a save change. |
| GM2 | Garrisons (Regicide) | Low | no | defenseManager.js:913-914: an emergency garrison is TYPE_PROTECTION when the holder heals (buffHeal; civic centre 1, temple 3), and TYPE_EMERGENCY only otherwise. garrisonManager.js:302-333 | simulation.cpp:31964-31975 | RTS0 applies the emergency rules to the critical hero in any holder. In a civic centre, where 0.28's hero falls under the protection rules (the hero is a Soldier, so an arrow class), 0.28 differs in two cases. It unloads the hero when nothing is around and its health is 0.7 or more (RTS0 keeps it until full health). It also unloads a melee hero at 0.4 health or more when only siege is near (RTS0 keeps it). | In `petra_keep_garrisoned`, take the TYPE_EMERGENCY branch only when `structure_garrison_heal(holder) <= 0`; in a healing holder, fall through to the protection rules. |
| W9 | Builders | Nit | no | worker.js:363-379: when UnitAI's autocontinue sends a builder to a Field, Petra drops target-foundation, stops it and reassigns it | simulation.cpp:19884-19891 (`next_after_construction`, Petra branch) | After a house, `find_nearby_foundation` may pick a field foundation, and the builder then builds the field. In 0.28 fields are built only by food gatherers through buildAnyField; a house builder sent to a field is taken off and reassigned. | In the Petra branch, when the nearby foundation found is `BLD_FIELD`, go idle (as for other structures) instead of building it. |
| Q9 | Plan start order | Nit (uncertain) | no | queueManager.js:371-394 (startNextItems runs in `queueArrays` priority order after every manager) with queueplanBuilding.js:29-31 (one structure a turn, `turnCache.buildingBuilt`) | simulation.cpp:34437-34451 (economy, then naval, opening, buildings, research); 24312 lays fields inside `petra_manage_economy` | The first structure placed in a turn takes the turn's one slot in manager order, not queue priority. A paid field (priority 480) laid in `petra_manage_fields` goes ahead of a paid storehouse (dropsites, 950) or farmstead (700) that same turn, and they move to the next turn. In 0.28 the higher priority starts first. Because RTS0 funds an item a turn late, two structures rarely become payable in the same turn, so the effect is likely small. | Collect each turn's ready structure plans, with their queue and placement, and lay the highest-priority one at the end of the turn. Or move `petra_manage_fields`' placement after `petra_check_resource_levels` and the farmstead. |

Impact order: Q8, H8, H9, W6, W7, W8, GM1 and GM2 are low (W8 on naval maps only, GM2 in Regicide only); W9 and Q9 are nits. Only GM1's exact form would need a save change.

State (2026-10-27): W6, W7, H8 and H9 are fixed (tests `test_far_sparse_gatherer_returning`, `test_idle_gatherer_keeps_load`, `test_corral_pays_from_queue`). The other rows are open and tracked in docs/REMAINING_GAPS.md as PC1-PC10.

## Checked and matching (or already covered)

**garrisonManager.js**
- update: the holder sweep every second turn (holderTimeUpdate, more than 3 s)
- the enemy scan: arrow range or 80 m centre to centre; Gaia units only in COMBAT; ranged structures; melee and ranged siege split
- allowMelee passed to garrisonUnitsInside
- unload, and recall of units still walking (G1)
- keepGarrisoned TYPE_PROTECTION branches: heal level, arrow classes, melee release, Support and siege under threat
- the capture-points rule cannot fire, since no 0.28 unit except the catafalque is Capturable
- gmin = ceil((decay - regen) / garrisonRegen)
- the justCaptured barracks rule (G5)
- numberOfGarrisonedSlots counting units on their way (`petra_garrison_incoming`)

**worker.js**
- hunting
  - startHunting filters: fast or ranged for animals that flee
  - 25000 m² for units that aren't FastMoving
  - enemy territory and an ally's cattle
  - food-dropsite distances 35000, 7000 and 12000 m²
  - nearest first, treasure first
- hunter subrole
  - the 20 s lastHuntSearch
  - another base's hunt, by ship if needed
  - drift into enemy territory
- the hunt-drift check: 90000 / 30000 m² in neutral land
- isInaccessibleSupply: 3 s and 10 s, one retry, then 600 s inaccessible
- findSupply tiers in order:
  - own base nearby, its fields, its field foundations, its medium
  - other bases on the same land
  - a dropsite foundation
  - across the water
  - faraway; for food only when allowDistantFood
- findSupply's amount/(1+n) < 30 and the "not in enemy territory" test
- gatherNearestField: penalty (1-rate)·160000, and the rate < 0.7 test with field foundations
- buildAnyField: builder room
- startFishing: nearest to our dock on the boat's sea, canFishSafely, an exhausted sea deletes the boat, drift home
- the GATHER recheck: crowded, or nearby/medium membership (W6 aside)
- a builder finishing a house autocontinues; others go idle (W2)

**queueManager.js and queue*.js**
- distributeResources: the account rescale after bartering, give-back of the excess, the floor and remainder pass
- switchResource: lower priority only, `switched` once per start
- startNextItems account reset
- checkPausedQueues tiers and needFarm/needCorral/needFish exemptions
- the attack plan batch dropping to 1 under workersMin (`train_for`)
- queue base priorities
- queueToReset (reset_on_start)
- a phase ResearchPlan is a rush: stopAllProduction(0.45) on the chosen researcher
- the researcher with the shortest queue
- TrainingPlan.start's trainer sort
  - shortest queue, +10 at a Civic trainer for non-support, +50 near an invading army for support
  - the "base with fewer workers" +1 tie-break is not ported (nit)
- the "first turn" firstTurn rates are dead code in 0.28: playedTurn is incremented before HQ.update

**headquarters.js economy**
- buildMoreHouses: getPopulationMax ≤ limit return, 3/5 plans, freeSlots with foundations, the 10/15/21 thresholds and saveResources, priority ×2 under 5 accounted free slots, researchPopulationBonus when unbuildable
- checkPhase: popPhase2 accounted population, workPhase3 workers, blocked by majorTech / civilCentre / wonder, hasResearchers
- checkPhaseRequirements: Town order market, temple, forge at priority 1000; foundations count; the 5 Village / 3 Town counts (phase_town and phase_city_generic)
- buildFarmstead: storehouse and house first, the %4==2 gate with saveResources/canBarter
- buildMarket: priority ×3, setAccounts/transferAccounts funding
- buildForge: popForForge, after a market
- buildTemple: city phase or Regicide, Vesta first
- buildWonder: 70/30% stock test, %30, 0.9 × population cap
- manageCorral's breeding count: owned animals plus queued, at most nCorral (H8 and H9 aside)
- hasActiveBase gates (H6)

**researchManager.js**
- update cadence: %5==1, active base, blocked while minorTech or majorTech holds a plan
- researchWantedTechs / researchPreferredTechs
  - champion unlocks and the Seleucid army reform first with priority ×2
  - the phase-1 / phase-2 "10 × workers" affordability skip against getAvailableResources
  - increasePriority under a 400 cost sum
  - the phase gates for preferred (≥ 2) and random (≥ 3)
- the random pick's healer/outpost/wall and unusedNoAllyTechs exclusions
- researchPopulationBonus and researchTradeBonus first-modification tests
- the order of modifications in one tech can change increasePriority (content; owned by E)

**chatHelper.js**
- each message kind has a call site matching 0.28:
  - launchAttack when a plan starts (huge vs other)
  - answerRequestAttack join/decline/other
  - sentTribute and requestTribute
  - newTradeRoute once per ally (warnedAllies)
  - emergency enter and exit
  - newDiplomacy and the diplomacy request answers (owned by Triggers)
- the "/allies" and "/msg" addressing
- the variant is picked without touching the simulation RNG
