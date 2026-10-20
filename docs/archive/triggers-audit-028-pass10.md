# Triggers: tenth pass against 0 A.D. 0.28 (2026-10-20)

## Method

- **Campaigns:** 0.28's gui/campaigns (setup, new_modal, load_modal, default_menu and its endgame), gui/common/campaigns (CampaignRun.js, CampaignTemplate.js), session/campaigns/CampaignSession.js, the campaign branches of session.js leaveGame and summary.js, pregame/MainMenuItems.js and the campaigns/*.json templates were read line by line against scenes/menu/campaigns.gd, scenes/menu/campaign_run.gd, scenes/menu/scenario_start.gd, the summary and quit-dialog exits and main_menu.gd. The gaps are rows CA1-CA14 in REMAINING_GAPS "Triggers, tenth 0.28 pass".

## Campaigns: matching

- The run file's format (data, meta, template_identifier) and its file name.
- Save, load and delete, with the delete question.
- Requires and ShowUnavailable; the Completed, Available and locked statuses.
- Start Scenario only when the level's requirements are met; a double-click starts it; cheats on.
- A level completed only on a live win, the moment it is won.
- After the match: the summary, or the campaign menu, with the lobby and replay checks in 0.28's order.
- Saved Games filtered to the run, with the compatibility filter on or off; saves and replays carry the run.
- Both Continue Campaign entries; Load Campaign sorted by file name.

Checked by tools/e2e_campaigns.gd, which covers each fixed row.
