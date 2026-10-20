# 0 A.D. 0.28 parity audit, pass 6 (areas: auras, Germans, reference pages, selection details, skirmish maps, poison/burning)

RTS0 tree: the repository (main, HEAD a8ac431f2). RTS0 paths below are relative to it.
0.28 paths:
- `sim:` = /mnt/project-files/d-civ-rosters/0ad-028-src/simulation/
- `gui:` = /mnt/project-files/session-ui/0ad-028-public-gui/gui/
- `maps:` = /mnt/project-files/maps/0ad-028-maps/

This pass skips everything verified in pass 2-5, the selection-details audit and docs/REMAINING_GAPS.md. Petra cadence and difficulty were not audited.
Helper script: skrep.py (skirmish unit replacements per civ vs start_entities_data.h), kept outside the repo.

**Result: 10 findings, all LOW or LOW-MEDIUM.** The simulation side of every area matched, except for one edge case (F3). Most findings are text in the reference pages and the session UI. None changes the save format.

## Findings, highest impact first

### F1. The Template Viewer and Structure Tree omit structure auras and the generic unit auras (LOW-MEDIUM, reference text)
- **0.28:** gui:reference/viewer/ViewerPage.js:160-173 InfoFunctions include `getAurasTooltip`, and gui:reference/structree/Boxes/EntityBox.js:85-90 TooltipFunctions do too. `template.auras` (GetTemplateDataHelper) lists every token-merged aura:
  - structures: temple_heal "Medical Treatment", iber_monument on the germ totem "Religious Fervor", xp_trickle "Rigorous Training" on barracks and stables, wall_garrisoned "Wall Protection", wonder_population_cap "Glorious Expansion", arsenal_repair, theater, library and so on;
  - units: units/ram_garrison "Battering Team" on all infantry, units/heroes/hero_garrison "Inspired Defense" on all heroes.
- **RTS0:**
  - scenes/reference/template_viewer.gd:463 and scenes/reference/structree_page.gd:95 read only `d.get("auras", [])`.
  - `RtsoReference::get_structure` (src/presentation/reference_data.cpp:414-480) never sets "auras". The structure aura text is already in data/reference/templates.json (`structures.<civ>.<type>.auras`, e.g. germ "9" → Medical Treatment, radius 40), but nothing reads it.
  - `get_unit` (reference_data.cpp:357-367) lists roster auras only. Pass 5 F2 added Battering Team and Inspired Defense to the session tooltips only (`seldet::unit_auras_tooltip`, src/presentation/selection_details.h:516-530), not to the reference pages.
- **Impact:** reference pages only. A player cannot see in the viewer or Structure Tree that temples heal, that the totem buffs attack, or that barracks train garrisoned units. The simulation is unaffected.
- **Fix:**
  - In info_text and the structree TipButton, fall back to `extra.get("auras")` for structures.
  - For units, append the two generic aura lines exactly as `unit_auras_tooltip` does. Infantry get Battering Team and heroes get Inspired Defense, first in token order: parent template auras before the roster's.
- **Save format:** no.

### F2. The Template Viewer shows technology costs without the researcher's TechCostMultiplier (LOW-MEDIUM, reference text)
- **0.28:** gui:reference/viewer/ViewerPage.js:77-85:
  > `const { techCostMultiplier } = this.TemplateParser.getEntity(researchers[0], this.activeCiv); ... this.currentTemplate.cost[res] *= techCostMultiplier[res];`

  The civ's own modifiers (civ bonuses and self-affecting team bonuses) are applied first: TemplateParser.js:361-369.
- **RTS0:**
  - scenes/reference/template_viewer.gd:137 only costs entries that are not techs (`if kind != "tech": d = Common.civ_costed(d, kind, civ)`), and `get_tech` (reference_data.cpp:482-492) returns the base cost.
  - The Structure Tree does apply it (structree_page.gd:76 `civ_costed(d, tip_kind, tip_civ, tip_researcher)`), so the two pages disagree.
- **Affected** (data/reference/templates.json `civs.*.tech_cost_mult`):
  - Athenian civic-centre techs: ×0.7 cost, ×0.5 time;
  - Gallic forge techs: ×0.85;
  - Mauryan temple and shrine techs: ×0.5;
  - Macedonian storehouse techs: time ×0.
- **Fix:** for "tech", pick the first researcher (`d["researchers"][0]["type"]`, else `d["researched_at"]["type"]`) and call `Common.civ_costed(d, "tech", civ, researcher)`.
- **Save format:** no.

### F3. The Kushite Temple of Amun's "Powerful Priesthood" (−5% hero health) does not stack (LOW, simulation edge case)
- **0.28:** sim:data/auras/structures/kush_temple_amun.json has `"type": "global"` and `"stackable": true`. Auras.js:21-27 then keys the modifier per source entity, so each Temple of Amun the player owns multiplies its heroes' Health/Max by 0.95. Owning two needs a captured one: the template_player.xml:34 limit is TempleOfAmun 1.
- **RTS0:**
  - The aura is registered as a range-1000 AURA_HEALTH source per temple (civ_roster_data.h:124 and civ_building_auras(BLD_TEMPLE_AMUN) = {106, 109}, civ_roster_data.h:154; simulation.cpp rebuild_auras ~34771).
  - `AuraManager::query_modifier` counts each aura once however many sources there are (src/simulation/aura_manager.h:244-252). So the multiplier is 0.95 at most, where 0.28 gives 0.95^n.
  - Theater (sim_types/simulation.cpp:1781-1786, ×1.2 per theater) and wonder_population_cap (simulation.cpp:795-797) do stack, so Amun is the only stackable aura handled as once-only.
- **Impact:** only when a player owns a captured Temple of Amun besides their own. Their heroes then have about 5% more health than in 0.28.
- **Fix:** give AURA_HEALTH a stackable flag per aura row, as AURA_BUILD already has (`builder_rate_range_auras()[row].stackable`), and skip the `once[]` check for kush_temple_amun.
- **Save format:** no. The state hash changes in that case only.

### F4. Wall, gate and outpost turrets appear in the UI as a garrison (LOW, UI text)
- **0.28:**
  - Civ walls, gates and outposts have a TurretHolder and no GarrisonHolder: sim:templates/template_structure_defensive_outpost.xml:67-75 and structures/<civ>/wall_*.xml.
  - Hovering one with soldiers runs `occupy-turret` (gui:session/unit_actions.js:767-790, specificness 21; `garrison`, specificness 20, returns false). That gives the cursor `action-occupy-turret` and the tooltip "Current turrets: %(occupied)s/%(capacity)s".
  - The details tooltip shows only "Turret Positions: N" (tooltips.js getTurretsTooltip; getGarrisonTooltip needs garrisonHolder).
  - The command row offers "Unload Turrets" ("Order all units to leave the selected turret points.", unit_actions.js:1463-1489), not "Unload All".
- **RTS0:**
  - `structure_garrison_cap` returns the turret count for these pieces (src/simulation/simulation.cpp:7775-7779), and turreted units live in `garrisoned_units`.
  - So the hover gives "Current garrison: x/y" with `action-garrison` (src/presentation/session_pointer.cpp:272-279).
  - The details tooltip shows "Garrison Limit: N" as well as "Turret Positions: N" (inspector_view.h:1393-1394, using b.garrison_cap).
  - The command row shows "Unload All" (command_queue_view.h:727-730).
- **Impact:** wording, cursor and one extra tooltip line. Behaviour is the same, and both games use the U hotkey.
- **Fix:** when `turret_points_of(b)` is set:
  - use the occupy-turret cursor and the "Current turrets" string;
  - skip `garrison_limit_tooltip`;
  - label the button "Unload Turrets" with session.unloadturrets.
- **Save format:** no.

### F5. Trade gain tooltip format, and the market-to-market rally tooltip is missing (LOW, UI text)
- **0.28:**
  - Every trade cursor tooltip uses `getTradingTooltip`: "Gain: 20+5 (Alice), 5 (Bob)", with the market gain of the trader's owner added after "+" and other owners listed by name (gui:session/selection_details.js:567-592; unit_actions.js:682-712).
  - Setting a market's rally point on another market shows "Right-click to establish a default route for new traders.\nExpected gain: …" ("Gain: …" when a trader is selected). A market that is too close gets the disabled cursor and the tooltip "This market is too close to the origin market." (unit_actions.js:1243-1275).
- **RTS0:**
  - session_pointer.cpp:623-629 `gain_text` gives "Gain: 20 (+5 to the markets' owners)", a string that is not in 0.28. The single-trader panel (inspector_view.h:993-1009) already uses the 0.28 format.
  - The rally path (session_pointer.cpp:93-94; session_commands.cpp:126-131) sets `RALLY_CMD_TRADE` with no tooltip and no too-close check.
- **Impact:** wording, and translations will not match. Inferred: a rally route to a too-close market is accepted where 0.28 refuses it.
- **Fix:**
  - Build `gain_text` with the same `%(gain)s (%(player)s)` logic as inspector_view.h:999-1008.
  - In the rally branch, compute `calculate_trade_gain(from, to)`: show the 0.28 text, and the disabled cursor when `g.trader == 0`.
- **Save format:** no.

### F6. Ships are left out of "Garrison Size" (LOW, UI text)
- **0.28:**
  - sim:templates/template_unit.xml:26-28 gives every unit `<Garrisonable><Size>1</Size>`. template_unit_ship.xml inherits it, and no 0.28 template disables it.
  - getGarrisonTooltip (tooltips.js:569-583) therefore shows "Garrison Size: 1 + N" for a ship carrying N.
  - displayMultiple (selection_details.js:404-408) counts each selected ship as 1 plus its passengers.
- **RTS0:** both places exclude ships, "a ship is not Garrisonable":
  - inspector_view.h:1353-1356: `unit_is_ship(...) ? String() : garrison_size_tooltip(1, passengers)`;
  - inspector_view.h:1037-1038: `(unit_is_ship(...) ? 0 : 1)`.
- **Impact:** tooltip numbers only.
- **Fix:** drop the ship exception in both places.
- **Save format:** no.

### F7. The Template Viewer has no "Upgradable to:" for units (LOW, reference text)
- **0.28:** ViewerPage.js:108-111 and InfoFunctions `getUpgradeText` cover the unit templates with `<Upgrade>`:
  - rome/infantry_spearman_e and infantry_swordsman_e (→ Centurion);
  - pers/champion_infantry and champion_infantry_archer_upgrade;
  - maur/hero_chandragupta_infantry;
  - template_unit_ship_fire (every fire ship).
- **RTS0:** template_viewer.gd:476-492 (the unit branch) has no upgrades block. The data exists (`civ_unit_upgrade_table()`, already used by structree_page.gd:320 through `get_trainer_units`).
- **Fix:** add the upgrade entries to `get_unit` and print "Upgradable to:" after Researches.
- **Save format:** no.

### F8. Structure Tree tooltip wording (LOW, text)
- **0.28:** EntityBox.compileTooltip appends `getTemplateViewerOnClickTooltip()`, which is "Click for more information." (gui:common/tooltips.js:1244). getAurasTooltip appends " Range: N meters" to range auras (tooltips.js:1093-1100).
- **RTS0:** structree_page.gd:109 says "Click to view more information.", and the aura lines at structree_page.gd:95-96 have no range.
- **Fix:** change the string, and reuse template_viewer.gd's aura line format, which already prints the range.
- **Save format:** no.

### F9. Template Viewer cost line order and format (LOW, text)
- **0.28:** `getCostTypes()` = resource codes, then "population", then "time" (tooltips.js:37-40). Each item is a resource icon followed by its number.
- **RTS0:** template_viewer.gd:173-183 prints "Food 50  Wood 50  Time 10 s  Population 1", with time before population, words instead of icons and an " s" suffix.
- **Fix:** put population before time and use the icon form; the structree TipButton reuses `cost_line` too.
- **Save format:** no.

### F10. The Civilization Overview has extra headings (LOW, text)
- **0.28:** gui:reference/civinfo/civinfo.xml shows the civ emblem, the civ name (sans-bold-20) and the History text at the top, then the four subsections. There is no "<Civ> Gameplay" or "History of the <Civ>" heading.
- **RTS0:** scenes/reference/civinfo_page.gd:97 adds the heading "%s Gameplay" and line 79 adds "History of the %s". These are older A23-era strings.
- **Fix:** drop both headings, and show the civ name next to the emblem.
- **Save format:** no.

### Notes (no player-visible effect today)
- **N1.** template_viewer.gd:186 has `DAMAGE_ORDER := ["Hack","Pierce","Crush","Poison","Fire"]`. 0.28's sim:data/damage_types orders Fire 4, Poison 5, as src/presentation/selection_details.h:375 already does. No 0.28 template has both types in one damage map, so nothing shows differently.
- **N2.** 0.28 displayMultiple assigns the "Carrying:" line with `=` (selection_details.js:430-437), which drops the "Garrison Size" line whenever the group carries resources. RTS0 (selection_details.h:305-314) keeps both. This is a 0.28 bug; copy it only if exact parity is wanted.
- **N3 (inferred).** Global Health/Max auras (Pyrrhus ×0.85, the Carthaginian, Iberian and German catafalques, Temple of Amun) reach garrisoned units in 0.28, because they are player-level modifiers. RTS0's `apply_health_auras` (simulation.cpp:34600-34612) skips `!alive` units, so a garrisoned unit's maximum health catches up only once it leaves. The final values are the same; only the time inside differs.
- **N4.** 0.28 Promotion TrickleRate grants XP in 1-second steps (Promotion.js:124 SetInterval 1000). RTS0 `tick_promotion` adds it continuously (`gain_xp(dt)`). A promotion can come up to 1 s earlier.

## Verified matching, no gap found this pass
- **Aura types:**
  - 0.28 has no "formation" aura files; RTS0 has none either.
  - garrison auras:
    - hero_garrison: +1 GarrisonRegenRate, once per structure, an ally's structure included (simulation.cpp:34719-34730);
    - ram_garrison: +0.15 per Infantry passenger, mutual allies included (simulation.cpp:10159-10170);
    - Caros Valiant Defender: ×1.75 arrows and maximum, an ally's holder included (simulation.cpp:7276-7295);
    - corral garrison: +1 food per animal, stackable.
  - garrisonedUnits auras:
    - arsenal +3, prytaneion +6 and Cothon +10 Health/RegenRate on matching classes of any owner (simulation.cpp:8649-8696);
    - xp_trickle on barracks, stable and elephant stable (and their children);
    - turretedUnits wall_garrisoned on walls, gates, outposts and rome/siege_wall_tower, not on the Mauryan rampart.
  - Capture regen is RegenRate + GarrisonRegenRate × each garrisoned unit's Capture attack, as Capturable.js:182-199 (simulation.cpp:14838-14846).
- **Aura on/off:**
  - a source that dies: its wreck has build_progress 0 and dead units are skipped;
  - a source that garrisons keeps its global auras and loses its range auras (rebuild_auras ~34775);
  - a change of owner or diplomacy is picked up by rebuild_auras every 0.4 s;
  - auras of a foundation or a Gaia structure apply nowhere;
  - range auras are measured centre to centre on structures.
- **Stacking:**
  - theater ×1.2 per theater;
  - wonder_population_cap ×1.2 per wonder, needing the owner's tech;
  - library once only;
  - every non-stackable aura applied once per aura key.
- **Germans:**
  - the log ram has `GarrisonHolder disable` (siege_garrison_capacity, simulation.cpp:7264-7273);
  - encampment: research list (CC list minus phases, metropolis and resettlement, plus wagon_trains), no BuildingAI, Upgrade cost and time;
  - great hall; the Seeress auras.
  - Skirmish unit replacements match for all 15 civs (p6/skrep.py). germ default_support_female_citizen has no 0.28 template, so it is destroyed, and RTS0 removes it too.
- **Skirmish maps:**
  - only gallic_fields_3p has a trigger script (already ported);
  - every `structures/{civ}/<x>` general target exists for every civ;
  - the house_5/house_10 rules (Han neither, germ house_10) and the Spartan wall and Iberian wall rules match (session_setup.cpp:355-366).
- **Poison and burning:**
  - the receiver tooltip uses "Interval:", "Duration" with the time left (Duration − elapsed), the receiver text, and no stackability;
  - the applier tooltip ends with " (stacks)";
  - up to 5 status icons;
  - the tick, stack and duration rules were already matched in pass 2-4.
- **Selection details:**
  - the "Current garrison" orange rule (occupied ≥ capacity) is equivalent for single-slot units;
  - the trader panel gain string, the rank tooltip and the status icon tooltips match.
- **Reference pages:**
  - the Civilization Overview bonuses (auto-researched techs, player auras, civ JSON CivBonuses, team bonuses), specific techs, structures and heroes;
  - the structree tech costs with TechCostMultiplier;
  - the attack, resistance, status-effect and heal lines in the viewer.

## Status
- Fixed in the PR that adds this file: F1, F2, F3, F4, F5 (gain format; the market-to-market rally tooltip is still open), F6, F8, F10.
- Open: F5's rally tooltip and too-close check, F7, F9.
