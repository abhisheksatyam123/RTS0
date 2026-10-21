# Triggers: thirteenth pass against 0 A.D. 0.28, campaigns and cinema (2026-10-21)

## Method

The campaign pages and level start (gui/campaigns/**: CampaignMenu.js, NewCampaignModal, CampaignRun.js, MapCache.js, PlayerName.js), session.js and summary.js endings, and CCmpCinemaManager.cpp, CinemaPath.cpp, NUSpline.cpp and MapReader's cinema reading were read against scenes/menu/{campaigns,campaign_run,scenario_start,match_settings}.gd, the summary and quit pages, cinema.h, tick_cinema and cinema_view.h. Rows CA1-CA14 and CI1-CI14 were not repeated. The same pass gave the campaign test maps' cities, sanctuary and armies their own templates (SU4).

## Findings

| # | 0.28 | RTS0 before | Now |
|---|---|---|---|
| CA15 | startScenario builds a fresh `new GameSettings().init()` (CampaignMenu.js:96) | `ScenarioStart.configure` set the map, seats, civs and teams on the MatchSettings autoload and kept the rest from the last Game Setup, your seat included | `MatchSettings.reset_match()` puts every match field back to its default first (the replay flag, the summary's music and the multiplayer name stay); you sit in seat 1 |
| CA16 | Your player is named playername.singleplayer, else the system user name (PlayerName.js:118-123) | "Player 1" | `singleplayer_name()` |
| CA17 | getLevelPreview falls back to the map's preview, then nopreview.png (MapCache.js:95) | The level menu hid the picture | `no_preview()`, as the setup page |
| CA18 | Enter in the new run's name does nothing (NewCampaignModal.xml) | It started the run | Dropped |
| CA19 | A level's MapType and useGameSetup | Ignored | Kept: no shipped campaign uses them |
| CI15 | DeletePath removes every queued copy, the playing one too (CCmpCinemaManager.cpp:214), keeping the elapsed and total times | Queued copies played on (CI12 had it backwards) | Matches; an emptied queue stops quietly, as CI5 |
| CI16 | ClearQueue is not in the script interface (ICmpCinemaManager.cpp:25-31) | CI5 read as script-visible | CI5 notes it |

## Matching

The tutorial levels' maps, their empty victory conditions and AI, the Introductory Tutorial's win and Economy Walkthrough's never-ending game; the order of exits after a campaign game and completion only on a live win; the run file and its label rules; the replay menu's Campaigns filter; Cinema Demo's path (21 nodes, 5 targets, 55.5 and 42.2 s, scaled by 0.25); the spline, easing and target sampling; the queue's reset and totals; the bars, hidden GUI and blocked input.

## Tests

`e2e_campaigns`: a level started after a setup with seat 2, 50 population, high resources, double speed, a revealed map and a ceasefire starts in seat 1 at the defaults, with your single-player name; the walkthrough shows the no-preview picture. `test_triggers`: DeletePath of a playing path with copies queued leaves only the next path, which ends 5 s after the deleted one started, and the emptied queue stops without OnCinemaQueueEnded.
