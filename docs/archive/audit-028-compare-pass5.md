# 0 A.D. 0.28 parity audit, pass 5

Scope: auras, Allied View, selection-details text, Petra cadence and difficulty, skirmish map metadata, unit art, snap-to-edges, poison and burning, and the Germans civ.
RTS0 checkout: the repository root (paths below are relative to it).
0.28 sources:
- simulation: /mnt/project-files/d-civ-rosters/0ad-028-src/simulation
- gui: /mnt/project-files/session-ui/0ad-028-public-gui/gui
- maps: /mnt/project-files/maps/0ad-028-maps

This pass excludes items already covered by pass2, pass3, pass4, audit-028-selection-details and docs/REMAINING_GAPS.md.

**Result: 4 findings.** That is fewer than the 6-10 hoped for. Earlier passes covered these areas thoroughly, and I list a finding only when both sides were checked against code or data. The areas that matched are listed at the end so they need not be re-audited.

## F1. Range auras also buff their own source (MEDIUM, inferred)

Impact: about 24 heroes, the Roman centurion and the German seer get their own range-aura bonus. RTS0 applies it; 0.28 does not (the 0.28 side is inferred). Examples:
- Leonidas, Brennus and Vercingetorix gain the attack or armour they give to nearby troops.
- Hannibal and Maharbal gain the bonus they give to nearby cavalry.
- The centurion gains his own +armour and +attack.
This shifts hero duels and the strength of small hero-led fights in a normal skirmish.

0.28 evidence:
- simulation/components/Auras.js:283-292: range auras use `cmpRangeManager.CreateActiveQuery(this.entity, 0, range, players, IID_Identity, ...)`.
- The engine RangeManager never returns the query's own source entity, so the source is never in its own aura's targets (inferred: CCmpRangeManager.cpp is not in the share).
- Global auras (`ApplyTemplateAura`, Auras.js:213-240) do include the source, so this applies to range and formation auras only.

RTS0 evidence:
- src/simulation/aura_manager.h:136 (`for_each_reaching`), 163 (`query_stacked`) and 187 (`query_modifier`) have no source-id exclusion.
- src/simulation/simulation.cpp ~34664-34700 (rebuild_auras unit loop) registers each unit's roster auras at that unit's own position with `register_source(id, ...)`.
- simulation.cpp ~15108-15118 (`unit_attack_effects`, `unit_repeat_time`) queries at the same position with the unit's own template and classes, so the source matches itself.
- Affected rows were found with p5/selfaura.py: the roster rows whose own range-aura class bitset includes themselves. Examples:
  - athen: hippocrates_1, iphicrates_1, pericles_1
  - spart: leonidas
  - rome: marcellus_1, scipio, centurion_1, centurion_2
  - cart: hamilcar_1, hannibal, maharbal
  - gaul: brennus, vercingetorix
  - germ: boiorix_1, lugius_1, germ_seer_2
  - also sele antiochus_iii and han liu_bang_2

Fix:
- Add a `uint32_t self_id` parameter (default `UINT32_MAX`) to `AuraManager::for_each_reaching`, `query_stacked` and `query_modifier`.
- Skip a source when `s.source_id == self_id && s.range < GLOBAL_RANGE`, so global auras still reach their source.
- Pass `units_.id[i]` (or the structure id) from every per-entity query in simulation.cpp: attack, repeat time, armour, walk speed, heal, vision, XP, capture, loot, gather and build rate.

Save format: unchanged. Simulation results and the state hash change, so replays recorded earlier will diverge.

## F2. Generic template auras are missing from the unit tooltips (LOW-MEDIUM, text)

Impact: two tooltip aura lines are missing in RTS0.
- 0.28 shows "Battering Team" (ram_garrison: +15% ram speed per garrisoned infantry) on every infantry unit's portrait and training tooltips.
- It shows "Inspired Defense" (hero_garrison: +1 garrison regen) on every hero's.
- RTS0 shows neither, so players never learn why rams speed up.
Note that the effect itself already exists: the germ spearman's roster row has aura slots -1,-1,-1, so the gap is in the tooltip text, not the simulation.

0.28 evidence:
- templates/template_unit_infantry.xml:22-24 declares `<Auras datatype="tokens">units/ram_garrison</Auras>`.
- templates/template_unit_hero.xml:12-14 declares units/heroes/hero_garrison.
- These are token-merged into every child. For example, germ/hero_teutobod resolves to hero_garrison, ram_garrison, germ_hero_teutobod_1 and germ_hero_teutobod_2.
- gui/common/tooltips.js:1078-1103 (`getAurasTooltip`) lists every template aura.
- It is used by selection_details.js:368-372 (portrait), selection_panels.js:207 and 1042 (training and production queue), and PanelEntity.js:140.

RTS0 evidence:
- src/presentation/inspector_view.h:1367-1375 builds the portrait aura lines only from `civ_unit_template(tmpl).auras`.
- So do command_queue_view.h:1914 (`auras_tooltip`) and panel_entities_view.h:233-238.
- data/reference/templates.json unit entries (e.g. germ/infantry_spearman_b) have no "auras" key.
- `structure_auras_tooltip` (command_queue_view.h:1927) covers structures only.

Fix:
- Have tools/extract_0ad_reference.py emit a units.<id>.auras list (aura name and description per resolved token).
- Append those lines in the three tooltip builders, de-duplicating against the roster auras.
- If the effect is ever moved into the roster instead, keep tooltip and sim in one table.

Save format: no.

## F3. Six skirmish size variants have no preview image (LOW-MEDIUM)

Impact: selecting one of these maps in game setup shows a blank preview; 0.28 shows a picture.
- corinthian_isthmus_2p
- cycladic_archipelago_3p
- greek_acropolis_2p
- median_oasis_4p
- neareastern_badlands_2p
- saharan_oases_2p

0.28 evidence: maps/0ad-028-maps/skirmishes/<map>.xml, ScriptSettings "Preview" gives, in the same order:
- isthmus_scenario.png
- cycladic_archipelago_skirm_3.png
- acropolis_3.png
- median_oasis_1.png
- neareastern_badlands_1.png
- saharan_oases_2p.png

(north_african_savanna and skirmish_demo have an empty Preview in 0.28 too, so they are not gaps.)

RTS0 evidence:
- scenes/menu/gamesetup.gd:1127-1145 (`map_preview`) loads res://assets/maps/previews/<id>.jpg and returns null when the file is missing.
- assets/maps/previews/ has 392 jpgs, none for these six ids.

Fix: make tools/fetch_0ad_previews.py resolve each map's Preview field (not just its id) and fetch those six. Or add an id-to-Preview alias table in `map_preview`.

Save format: no.

## F4. Capture tooltip label wording (LOW, text)

Impact: small wording difference. RTS0 says "Capture points:" where 0.28 says "Capture Points:" (title case).
- It shows on the multi-selection capture caption.
- It shows on the single-structure capture bar tooltip; there 0.28 uses the bare "Capture Points" when the caption is visible.
- It breaks string matching against 0.28 translations.

0.28 evidence:
- gui/session/selection_details.js `displayMultiple` calls getCurrentHealthTooltip with the label "Capture Points:".
- `displaySingle` sets captureTooltip to translate("Capture Points").

RTS0 evidence:
- src/presentation/selection_details.h:47 has `tr0("Capture points:")`.
- It is used by inspector_view.h:1137 (cap_vbar tooltip) and 1573 (multi capture caption).

Fix:
- Change the msgid to "Capture Points:" in selection_details.h:47.
- Add a bare "Capture Points" variant for the single-structure bar tooltip at inspector_view.h:1137.

Save format: no.

## Verified matching, no gap found this pass

- **Auras (data):** every aura matches 0.28 on affects classes, radius, players and values; this was checked mechanically (p5/aff.py).
  - Lugius' vision-add is ×0.28, by convention.
  - Unmodelled auras are no-ops in 0.28 too:
    - kush_hero_arakamani_2 uses the invalid Heal/HP path;
    - agis_2 and teutobod_2 have no modifications;
    - han_minister_garrison rows are empty placeholders.
- **Rosters:** unit stats (p5/stats.py) and technology civ masks and costs (p5/techciv.py) match. The only differences are explained:
  - folded civ bonuses and catafalques;
  - cart ship_movement_speed is not at the cart dock;
  - han siege_bolt_accuracy is not at the Han arsenal.
- **Allied View:**
  - InitGame.js:63-64 researches SharedLosTech for every player, matching RTS0.
  - The Cartography tech data matches.
  - Every 0.28 skirmish map has AllyView false.
- **Skirmish map metadata:**
  - name, description, keywords, CircularMap, player count and size match (p5/maps.py, maps2.py);
  - the gallic_fields trigger is ported.
- **Germans:**
  - SkirmishReplacements, house size, wagon trains and wagon_house promotion, and AI names match;
  - assets/textures/ui/emblems/germans.png exists. The sim_types.h:3272 comment about borrowing the Gallic emblem is stale, but nothing is wrong.
- **Petra:** these difficulty gates match:
  - neededDefenders, decaying structures, Sandbox gates, targetSize and tradeManager;
  - the advanced-buildings list;
  - the checkEnemyUnits rotation.
- **Poison and burning:**
  - resistance applies;
  - no Attacked reaction (UnitAI.js:4356 fromStatusEffect);
  - structures take full Fire damage.
- **Snap-to-edges:** the isSnapToEdgesEnabled equivalence and the hover angle reset match.
- **Unit art:**
  - all 423 draw GLBs exist;
  - fire ships, the catafalque and the German class models resolve.

## Status
- F1, F3, F4: fixed in the PR that adds this file.
- F2: open (Compare thread, next).
