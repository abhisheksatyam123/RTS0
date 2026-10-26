# Petra 0.28 vs RTS0: towers, garrisons, capture, relics, colonies, patrol

Status (Formations, 2026-10-20): fixed H1, H2 (save v104), M1-M5, L1-L3, L7, L9, L10, L12, and REMAINING_GAPS R6 (rootCount).
Open, in REMAINING_GAPS G46: L6 (L4, L8 and L11 fixed later; L11's ferried guards saved in v115) (L5 does not apply: RTS0 units are not capturable). L4 and L6 were tried and held back: each changes when units leave a holder, which breaks test_defense_switch_to_attack and test_defense_animals.


Audited at RTS0 HEAD 1ef009261. JS = /mnt/project-files/ai/0ad-028-petra, sim = src/simulation/simulation.cpp.
Defense (defenseManager/defenseArmy) and attack-plan internals were left out, as asked. Every claim below comes from reading the code.

## HIGH

H1. Relic expeditions are bare orders, not a raid plan. Nothing reserves their units.
- JS victoryManager.js:691-704 calls raidTargetEntity (attackManager.js:699), forceStart(), and sets the units' plan.
  The plan keeps them: defenseManager garrisonUnitsInside skips units that are WALKING or ATTACKING (defenseManager.js:808).
  Abort (attackPlan.js:2075) walks them back to the rally point or the nearest base.
- RTS0 sim:30785 calls order_capture_relic(expedition). petra_unit_reserved (sim:27579) does not cover ORDER_CAPTURE_RELIC.
  So defense recruitment (sim:29704) and petra_garrison_units_inside (sim:29241) can pull expedition units away.
  On abort, sim:30714-30718 calls cmd_stop, which leaves the units standing where they are.
- In a match: in Capture the Relic, expeditions break up whenever there is any defense alert. Units stopped by an abort stay idle deep in neutral or enemy land.
- Save change: no, for reserving or a walk-home. Yes, if done as real raid plans (petra_started_attacks_ is already saved, v52 kinds).
- Fix: count ORDER_CAPTURE_RELIC units as reserved in petra_unit_reserved. On abort, walk them to the nearest base anchor on the same land.

H2. RTS0 keeps no list of targeted Gaia relics, so it never re-sends to a relic it saw once.
- JS victoryManager.js:27,582-589: targetedGaiaRelics persists (serialized at :745). Every 10th turn, each targeted relic is reinforced by captureGaiaRelic.
  It needs no vision or territory check. With plans.length > 0 the 1/100 minimum is skipped (:686).
  The entry is dropped only when the relic changes owner (:291-292, :731).
- RTS0 sim:30728 treats a relic as already targeted only while one of our units holds ORDER_CAPTURE_RELIC on it.
  Once the expedition dies or is pulled away (H1), the relic counts as new. It needs our territory or a unit's vision again (sim:30729-30742) and the 1/100 minimum.
- In a match: 0.28 keeps feeding units to a relic it saw once, until someone captures it. RTS0 gives up after one failed expedition.
- Save change: YES. Add a per-AI list of relic ids (plus plan refs if H1 uses plans) to tick_state_io.
- Fix: keep petra_targeted_relics_ (relic ids), cleared when the relic's owner is no longer Gaia. Use it in place of the "capture <= -regen" test.

## MEDIUM

M1. Military production holds back a hard-coded, wrong fortress price (an RTS0 addition).
- JS has no reserve of this kind. The first fortress only gets 2x defenseBuilding priority once its plan is queued (headquarters.js:1608-1609).
  It is queued only if canBuild, at least 25 population per fortress (:1604), and fewer than 2 fortress foundations.
- RTS0 sim:19726-19731: in phase 3, not saving resources, with no BLD_FORTRESS of any state (wrecks count, no hp or wrecked check),
  attack-plan training keeps PETRA_FORTRESS_COST. That constant (sim:19490) is {wood 300, gold 200, stone 300}.
  The 0.28 fortress template costs wood 300 and stone 600, no gold. The reserve applies even when the fortress can't be built or queued.
- In a match: City-phase army training stalls on a wrong gold reserve and too little stone, until a fortress stands.
  A destroyed fortress's wreck switches the reserve off.
- Save change: no.
- Fix: drop the reserve and let the 0.28 queue priorities do the work. Or reserve only while d.pending == BLD_FORTRESS+1, using petra_structure_cost(BLD_FORTRESS).

M2. Fortresses and storehouses become extra PetraBase anchors.
- JS: only a CivCentre (military colony included) anchors a base. A captured non-decaying dropsite makes an anchorless base (basesManager.js:286-291).
  A fortress never anchors a base.
- RTS0 sim:19184 collects BLD_FORTRESS and BLD_STOREHOUSE (towers are filtered out at sim:19282).
  sim:19278-19300 then makes each one more than 30 units from any base, in our territory, a base, up to 4 in all.
  This applies to our own buildings, not only captured ones.
- In a match: petra_scuttle_undefendable (sim:29478-29486) treats a structure on a land with only a fortress or storehouse "base" as anchored, so it isn't scuttled.
  petra_retreat_critical_ent (sim:30522) can walk the regicide hero to a fortress or storehouse.
  Bases' assigned_workers and resources_depleted are computed around storehouses.
- Save change: no (bases are already saved, and the fix removes entries).
- Fix: only civic centres and colonies anchor a base. Give a captured dropsite an anchorless base, as basesManager does.

M3. headquarters.updateCaptureStrength and worker.js's capture re-check have no counterpart.
- JS headquarters.js:2113-2161 runs every turn more than 3 s after the last (:2343). For every own unit in COMBAT on a capturable target,
  it pools capture strength across all units (capturableTargets) and switches each order between Capture and attack (allowCapture).
  worker.js:202-211 does the same for a worker that UnitAI sent to capture an enemy structure.
- RTS0 calls petra_allow_capture only for attack squads (sim:26311, 26415) and defense armies (sim:30241, 30302). Strength is pooled per squad, not across all units.
- In a match: Petra workers and units outside plans that fight back at an enemy tower or fortress keep capturing a garrisoned one they can't take, rather than damaging it.
- Save change: no (capturableTargetsTime can come from the turn counter).
- Fix: add petra_update_capture_strength() at the end of the HQ turn. For own units with target_building_id set and not reserved, apply petra_allow_capture with all of them as the squad, and re-issue the attack with that flag.

M4. Critical-entity guards (relic, hero, wonder) are picked from champions and army units.
- JS victoryManager.js:450-452: tryAssignMilitaryGuard refuses any unit whose plan !== undefined.
  That rules out units in attack plans and every plan -1 unit, so the outOfPlan loop (:425) never assigns anyone.
  In effect guards are worker citizen soldiers and units never put in a plan.
- RTS0 sim:30626-30642 takes free champions first (pass 0), then any free soldier, including army units in petra_army_ids_.
- In a match: in CTR, Regicide and Wonder, RTS0 parks up to 4+5*defensive champions or army soldiers on each relic, hero and wonder (10+5*def for a wonder).
  0.28 uses gathering citizen soldiers, so RTS0's later attacks are weaker.
- Save change: no (petra_army_ids_ is already saved, v52).
- Fix: in free_for_guard, require petra_is_worker(i) (not in petra_army_ids_) and drop the champion pass.

M5. Barracks are garrisoned for decay even when they were never captured.
- JS garrisonManager.js:364 garrisons a decaying Barracks-class structure only when justCaptured (headquarters.js:170).
  An own barracks that starts decaying (TerritoryDecayChanged, :256) goes to HQ.decayingStructures. That is the destroy-if-being-lost check (:268-301).
- RTS0 sim:29408 garrisons any decaying BLD_BARRACKS of ours (the comment admits this). It also misses the other Barracks-class buildings: han academy and ptol mercenary_camp.
- In a match: when Petra's own barracks is cut off (say its CC falls), capture-capable units are pulled into it.
  0.28 would leave it, or destroy it when an enemy holds 20-50% of its points.
- Save change: YES. 0.28 serializes garrisonManager.decayingStructures. RTS0 needs the set of captured barracks ids in tick_state_io.
- Fix: record barracks ids Petra captured (stat_building_captured, sim:9865), use that set in petra_decay_garrison_min, and use the Barracks class, not the type.

## LOW

L1. Captured relics walk home differently.
- JS victoryManager.js:301-303 gives one order, on capture, through pickCriticalEntRetreatLocation (:465-484).
  It goes to the getBestBase anchor (CC or colony) only if that anchor is on the relic's land.
- RTS0 sim:30694-30708: every 10 turns, any idle relic of ours away from the nearest finished CC walks there.
  It ignores land and colonies, and aims at a fixed east-side point.
- Save change: no.
- Fix: use bases on the same land (petra_same_land); military colonies count.

L2. A relic captured first by another player does not abort an attack plan aimed at it.
- JS abortCaptureGaiaRelic (victoryManager.js:727-735) aborts every plan in the relic's targetedGaiaRelics entry.
  That includes attack plans on player 0 that picked the relic (attackPlan.js:973-974).
- RTS0 sim:30711-30712 only stops loose capturers ("Attack plans keep theirs").
- Save change: comes with H2's list. Fix: on the owner change, abort the started plan whose target_building_id is the relic.

L3. Expedition candidates.
- JS (victoryManager.js:660-672) also takes units from an unexecuted non-raid attack plan.
- RTS0 excludes them: petra_unit_reserved, via petra_in_attack_squad (sim:27457), checks petra_attack_plan_.
- Fix: in petra_capture_gaia_relics, skip only started plans and raids, and remove the taken units from petra_attack_plan_.squad.

L4. The garrisonManager holder check runs every turn.
- JS garrisonManager.js:134 checks a holder only after more than 3 s, which is every 2nd turn.
- RTS0 petra_manage_garrisons (sim:29496) unloads and recalls every turn.
- Effect: faster unloads. Fix: run that block on petra_turn_mod(2). No save change.

L5. keepGarrisoned TYPE_PROTECTION has no capture-points rule.
- JS garrisonManager.js:306-308 keeps a garrisoned unit while it holds less than 80% of its own capture points. That means siege engines, which are capturable.
- RTS0 petra_keep_garrisoned (sim:29215-29233) has no such rule. A ranged siege engine is released when no melee siege is near (sim:29232).
- Fix: if the unit has capture points, keep it while ours / total < 0.8. No save change.

L6. Hostile fauna don't count as threats in petra_around.
- JS garrisonManager.js:138-170 counts every enemy entity in COMBAT, Gaia fauna included, as around.unit.
- RTS0 petra_around (sim:29184) loops units_ only, not fauna_.
- Effect: attacking wolves or elephants don't keep infantry in a tower. Fix: also scan fauna_ in ACTION_ATTACK.

L7. Gaia defensive structures don't count in isUnderEnemyFire.
- JS headquarters.js:2096-2110 uses getEnemyStructures, which includes player 0.
- RTS0 sim:24885 uses petra_foe, which rules out GAIA_PLAYER.
- Effect: Petra may place a tower or fortress under a Gaia tower's fire. Fix: also accept b.team == GAIA_PLAYER.

L8. No allied fallback in findDefensiveLocation.
- JS headquarters.js:1149-1154: with no enemy and no neutral structures and no allied victory, allied CCs, fortresses and towers are used.
- RTS0 collect(false) at sim:24813 (see sim:24802-24805) always excludes allies.
- Effect: none in normal games. Fix: add a third collect pass over allies when last_man_standing().

L9. The first fortress without a frontier spot retries every turn.
- JS: a fortress plan whose findDefensiveLocation fails still set fortressStartTime and then gets setUnbuildable for 90 s (queueplanBuilding.js:61, headquarters.js:1607).
- RTS0 sim:24992: with no spot, fortress_start is not set, so a later fortress goes up as soon as a spot appears.
  Towers and sentries already do the 90 s wait (sim:25006, 25019).
- Fix: same kNoRoomWait handling for fortresses. No save change (fortress_start is saved, v56).

L10. Tower counting misses some tower types.
- JS counts the Tower class, which includes Han bolt and artillery towers (headquarters.js:1621, 1639).
- RTS0 sim:24961 counts only is_defense_tower or sentry.
- Effect: only captured Han towers. Fix: use building_tech_classes(b.type) & TC_TOWER.

L11. Guards on another land.
- JS victoryManager.js:409 runs a second pass, checkForSameAccess=false, and ships the guards over (requireTransport, :548).
- RTS0 sim:30616-30623 takes guards from the same land only. Fix: a second pass that uses petra_require_defense_transport.

L12. Repairs and building: the military colony isn't treated as a base anchor.
- JS baseManager.js:966 and :979 key on CivCentre and baseAnchor, which includes colonies.
- RTS0 sim:20291 uses anchor = is_civic_centre only, so colonies don't get the 5/7 repairers or the under-attack exception.
- Fix: add BLD_MILITARY_COLONY.

## Checked and matching
- buildDefenses: cadence (every 3rd turn, not Sandbox, sim:23973). saveResources && !canBarter gate. Fortress conditions (phase 3 or City researching, lapse x(1+0.1n), the bases+1+extra cap, pop/25, fewer than 2 foundations).
  Sentry conditions (numSentryTowers from difficulty and personality, Tower+WallTower count, saveResources lapse). Stone tower conditions (2*bases+3+extra, pop/8, fewer than 3 Tower foundations, priority 0.7x past the count).
  First fortress at 2x priority. queueToReset. Lapse times scaled by defensive (sim:24912-25029 vs headquarters.js:1591-1648, config.js:242-270).
- findDefensiveLocation: enemy CC/fortress/tower within 80 m vetoes a cell. 30 m from our towers and fortresses. CC-nearest score.
  Frontier masks (45/80 m; towers narrow only). Wonder mode (50 m + footprint, x200). isNearInvadingArmy (12000 m2). isUnderEnemyFire radius = half footprint.
  Fortress +8 m and tower 0 m placement gap. First fortress falls back to the standard placement (queueplanBuilding.js:167-174).
- Petra builds no walls or palisades, upgrades no towers, and gives no patrol orders. No "patrol", "Encampment" or wall-building code exists in 0.28 Petra, and none in RTS0's Petra.
- Military colony: chosen over the CC once we own our civ's CC (headquarters.js:1574-1581 = sim:24497-24499). Colony distances 220 m / reduce 30 (sim:24617, 24715). Counted as a base anchor and in numActiveBases.
- Army camp (Rome): not placeable in own territory in either, so the next advanced building is tried (sim:24046-24048). Ptolemaic mercenary camp counted as Barracks (sim:23934).
- Tower research: tower_range (MaxRange) is a wanted tech. tower_watch and tower_garrison (DefaultArrowCount) are preferred, cheap ones at 2x.
  Crenellations and murder holes go to the random City pick (researchManager.js:81-171 = sim:25390-25440).
- keepGarrisoned for heal, arrow classes, melee, support and siege, and the EMERGENCY hero rules. allowMelee kept in sync.
  Units still walking in are recalled. Our units in an allied holder are unloaded too (garrisonManager.js:175-196 = sim:29215-29233, 29496-29539).
- Decay garrison minimum ceil((decay-regen)/garrisonRegen), only for structures with defensive fire. TYPE_DECAY units stay while decaying.
- HQ decaying-structure destroy: 50% minimum, enemy (not Gaia) points, ratioMax 0.7 (0.85 when attacked) + rand 0.1, off below Easy (headquarters.js:265-302 = sim:29420-29448). Captures feed it (sim:9866).
- Captured CC: becomes a new base with defenders (sim:19241-19275). A captured unit is just another own unit (0.28 resets its metadata, headquarters.js:177-195).
- victoryManager cadence (every 10 turns). Relic capture thresholds: -regen, enough at 1/50 of points, expedition up to 1/25, new one needs 1/100 (sim:30722-30784 vs :675-706).
  Own-territory relic ends the new-relic search. Vision rule outside enemy territory. Relics count as critical, with guards 4+5*defensive.
