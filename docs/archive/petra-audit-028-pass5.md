# Petra bases, trade, naval and headquarters economy, fifth pass against 0.28

Read-only audit of RTS0's Petra port (area C) against 0 A.D. 0.28 Petra: `baseManager.js`, `basesManager.js`, `buildManager.js`, `tradeManager.js`, `navalManager.js`, `transportPlan.js`, the economy parts of `headquarters.js`, `researchManager.js` and `startingStrategy.js`. Rows already in pass 4, the Compare pass-6 Petra audit or REMAINING_GAPS are left out (G14, P4/P6, G49, pass6 #3 and 5c).

| ID | Area | Impact | Save | Summary | State |
|----|------|--------|------|---------|-------|
| B1 | Bases | Medium | save | 0.28 updates one anchored base per turn (basesManager.js:767); RTS0 runs every base every turn, so idle re-tasking, foundation staffing and dropsite used/lost counts run N times as often | Open |
| T1 | Trade | Medium-High | no | A trader that flees (or is otherwise busy) with a route was ordered back to trade every AI turn, dropping its goods; 0.28 orders only idle traders (tradeManager.js:133) | **Fixed** |
| T2 | Trade | Medium | no | A prospected market that could not be placed that turn was dropped; 0.28 queues the plan until it starts (tradeManager.js:664) | **Fixed** |
| H1 | First base | Medium (High on Nomad) | no | The first civic centre goes at the first free spot within 96 m of the units; 0.28 runs findEconomicCCLocation with proximity over the whole land (startingStrategy.js:341) | **Fixed** |
| H2 | Workers | Medium | no | Garrisoned and shipped workers drop out of trainMoreWorkers, getAccountedWorkers and checkBaseExpansion counts; 0.28 keeps counting them | **Fixed** |
| B2 | Builders | Medium (Mauryans) | no | The Mauryan worker elephant never builds; 0.28 gives it the worker role and prefers it for nearby foundations (baseManager.js:592, 934) | Open |
| H3 | Houses | Medium-Low | no | While phasing, the relaxed house placement (requireHouses) never ran: the retry hit the 90 s no-room mark; 0.28 calls setBuildable first (headquarters.js:1464) | **Fixed** |
| T3 | Transport | Medium-Low | no | No crossing plan is made without a free ship, so none asks for one to be trained; 0.28 makes the plan with needTransportShips and checkLevels trains a ship (transportPlan.js:82, navalManager.js:486) | Open |
| B3 | Bases | Low-Medium | save | handleNewBase's effects (saveResources, saveSpace, maxFields cleared for good) fire when the foundation is laid, and maxFields comes back if the second civic centre falls; 0.28 fires on ConstructionFinished | Open |
| B4 | Repairs | Low-Medium | no | Idle workers don't repair a damaged civic centre (baseManager.js:708) | Open |
| H4 | New bases | Low-Medium | save | A new base with no valid spot neither funds the CC plan, blocks phasing, nor sets the CC no-room mark (buildNewBase queues before searching) | Open |
| H5 | Houses | Low-Medium | save | 0.28 holds up to 3 (5) house plans and can start one a turn; RTS0 lays at most one house per 4th turn | Open |
| T4 | Transport | Low-Medium | no | Ships sail 8 s after ready with 2 aboard, or at 28 s with whoever is aboard; 0.28 waits for all, retrying blocked units 5 times | Open |
| T5 | Transport | Low-Medium | no | The pickup point is the last dry cell toward the destination, not getBoardingPos on the ship's sea (transportPlan.js:466) | Open |
| B5 | Builders | Low | no | Any idle worker on the map is picked before a gatherer next to the foundation; 0.28 prefers idle builders only within 200 m, then distance with food gatherers x3 | **Fixed** |
| B6 | Builders | Low | no | With no active base 0.28 lets every worker build each foundation; RTS0 raises only the cap | Open |
| B7 | Gatherers | Low | no | Military colonies are not dropsites for the exhausted-resource check, so stone or metal near only a colony is never gathered | **Fixed** |
| B8 | Builders | Low | no | targetNB uses BLD_BARRACKS and BLD_HOUSE; 0.28 uses classes, so the apartment, academy and Ptolemaic mercenary camp get 2 builders | **Fixed** |
| B9 | Dropsites | Low | no | The civic centre's dropsite stays shared with allies; 0.28 unshares it (baseManager.js:248) | Open |
| H6 | Cadence | Low | no | trainMoreWorkers, buildMoreHouses, buildFarmstead and manageCorral run with no finished base anchor; 0.28 skips them (headquarters.js:2281) | **Fixed** |
| H7 | Dropsites | Low | no | The fallback storehouse goes down while a civic centre is wanted but unpaid; 0.28 skips it while civilCentre has a queued plan (baseManager.js:507) | **Fixed** |
| T6 | Transport | Low | no | The roomiest free ship is picked, not the nearest; the +50 siege bonus applies to the merchantman | Open |
| T7 | Trade | Low | no | Any idle merchant ship becomes a trader; 0.28 switches one transport per 5 turns and only below targetNumTraders | Open |
| T8 | Fishing | Low | no | Every linked sea gets 2 fishing boats on a small start land; 0.28 raises only seas already wanting some (navalManager.js:171) | **Fixed** |
| T9 | Ships | Low | no | Ships can train on several seas at once; maintainFleet returns while any ship is queued (navalManager.js:509) | Open |
| T10 | Docks | Low | no | A dock with no room is searched again every 10 s (0.28: 90 s); the super dock uses pop_current and ignores a busy militaryBuilding queue | Open |
| T11 | Transport | Low | no | A ship sunk while sailing re-ferries survivors on the first stranded land only | Open |

Not ported on purpose: B10, a 0.28 bug (basesManager.js:121 calls `reset(type)` with the wrong arguments, so a reused base gets no defenders); RTS0 gives every new base its defenders.

## Details

- **T1.** `petra_manage_trading` now leaves a trader that is neither idle nor trading (fleeing, walking home, boarding). Test: test_market_dock_route's fleeing trader.
- **T2.** `petra_prospect_market` keeps `petra_prospect_.active` on until the market foundation is placed; a no-room failure waits out the 90 s mark as the queued plan does.
- **H3.** On buildMoreHouses' turn (%4==1) the house's no-room mark is cleared before the requireHouses retry.
- **H2.** `petra_unplanned_workers`, `petra_accounted_workers` and checkBaseExpansion's unit count include garrisoned units (in a structure or aboard), as `pop_current` does. Test: test_garrisoned_workers_count.
- **H6.** `petra_has_active_base()` (a finished civic centre or military colony) gates trainMoreWorkers, buildMoreHouses, buildFarmstead, manageCorral and research; assignToFoundations' "no base" also uses it, so a colony counts (part of B6).
- **H7.** The fallback storehouse waits while the civilCentre queue holds an unpaid want.
- **B5.** Idle workers are preferred only within 200 m of the foundation.
- **B7.** Every dropsite structure (military colony, imperial court, crannog, wagon encampment) counts for isResourceExhausted.
- **B8.** targetNB and the house +2 test classes Barracks and House (the academy, mercenary camp and apartment too).
- **T8.** A small start land raises only seas already wanting fishing boats.
- **H1.** `petra_build_first_base` asks `petra_find_economic_cc_location` for wood with proximity to the units' starting point (norm 0.5/(1+d²/250²), 400 m from a ship or a land under 24000 m², 8 m more room, cut 30, the units' land unless we have a transport ship). RTS0 keeps its old nearest-free-spot search as a fallback when no tile reaches the cut, where 0.28 would wait or try an oversea dock. Test: test_nomad_first_base's wood distance.

## Checked and matching

trainMoreWorkers (batch sizes, early returns, support ratio, soldier pick), buildMoreHouses thresholds, buildFarmstead, checkResourceLevels and dropsite quality, findEconomicCCLocation and findStrategicCCLocation (non-proximity), checkBaseExpansion, buildNewBase template choice, canBuild, researchManager cadence and lists, configFirstBase, the HQ update cadence; assignResourceToDropsite tiers, assignToFoundations ratios and targets, anchors and defenders, gather-rate caches, buildManager marks; barter, trader targets and templates, checkRoutes, market prospection triggers, findMarketLocation, isNewMarketWorth, dock triggers and findDockPosition, getBestShip, wantedFish, setMinimalTransportShips, fishing safety, requireTransport joining, drop-off points.
