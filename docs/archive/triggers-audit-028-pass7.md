# Triggers: seventh pass against 0 A.D. 0.28 (2026-10-19)

This pass covers campaigns, cinema, scenario-only units and lobby ratings. It skips what passes 1-6 (triggers/audit-028*.md in the shared folder, docs/archive/triggers-audit-028-pass6.md) and docs/REMAINING_GAPS.md already list.

## Matches

- **Campaigns:**
  - The run file and reading older run files.
  - Broken runs, deleting a run, and the new-run file name.
  - markLevelComplete, meetsRequirements and the level states.
  - Where the game goes after the summary, completion recorded on a win, and Continue/New/Load Campaign.
- **Cinema:** checked line by line against CCmpCinemaManager.cpp:124-261.
  - Turn updates, queue end, and adding to the queue.
  - ClearQueue/DeletePath keep TotalTime.
  - SetEnabled refuses an empty queue.
  - The cinema_demo schedule.
  - The camera holds while paused.
- **Scenario-only units:** every template a 0.28 trigger script spawns exists with 0.28 stats. Variants that differ only in art, requirements or build time spawn as their parent.
- **Lobby ratings:**
  - The Elo adjustment, the 1v1 rule and the 1200 start.
  - Merging match reports.
  - Leaderboard top 100, profile rank and win rate.
  - A game's rating as the average of its players, and the rating filters.

## Differences found (all fixed in this PR, no save change)

| # | 0.28 | RTS0 before | Fix |
|---|---|---|---|
| S1 | The Marian Legionaries `rome/champion_infantry_swordsman_02`..`_05` have their own stats: `_02` promotes to `_03` at 150 xp, `_03` (Hack 18.5) to the Centurion `_04` (Hack 20, 140 HP) at 300, and the Praetorian Guard `_05` (Hack 25, 180 HP) doesn't promote. Egypt 3v3 places them, Gallic Fields spawns `_02` waves, and Extinct Volcano garrisons `_02`. | They spawned as plain Italic Heavy Infantry (Hack 16, 200 HP, promoting at 3000 xp) | Four roster rows (`MAP_ROWS`, appended, ids 507-510). The promotion, motion and attack tables were regenerated, and Gallic Fields and Extinct Volcano spawn `_02` |
| S2 | campaign_test_map(_2_heightmap) places campaign armies and cities (simulation/templates/campaigns/) | 36 structures with no type were dropped, so the Conquest match ended at once | The cities become Macedonian civic centres and temples, and the armies become their hero or champion swordsman (`CAMPAIGN_STAND_INS` in port_0ad_maps.py). The territory_pull markers are skipped |
| K2 | The campaign label and the new-run name use the translated template Name and the translated "%(userDesc)s - %(templateName)s" | Raw English Name | Translated |
| L1 | EcheLOn creates a player row on join, so a member with no finished game has a profile ("-", 0 games) | The profile returned 404, and the lookup said "not found" for someone online | The row is created on login |
| L2 | The player list sorts by status, name or rating. An unrated player shows "-" in the status colour | Not sortable; blank rating | Sortable columns (name ascending first), "-" in the status colour |

## Left as is

- **K3:** levels missing from a campaign's `Order` sort last in RTS0 and first in 0.28. Every shipped campaign lists all its levels.
- **OnCinemaPathEnded:** carries the path index in RTS0 and the name in 0.28. No shipped map reads it.
- **The four new rows' art and sounds:** they use the Roman champion swordsman's.
