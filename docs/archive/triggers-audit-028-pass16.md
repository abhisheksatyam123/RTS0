# Triggers: sixteenth pass against 0 A.D. 0.28, the tutorials (2026-10-27)

## Method

maps/tutorials/introductory_tutorial.js (27 goals) and starting_economy_walkthrough.js (32 goals), their maps, maps/scripts/Tutorial.js and the session tutorial panel (tutorial_panel.xml, messages.js) were read line by line against tutorial_data.h, the tutorial goals in simulation.cpp, tutorial_panel.h and data/maps/introductory_tutorial.json and starting_economy_walkthrough.json. Rows G60 (panel layout), G37 (last Quit), U16 and CA15 were not repeated.

## Findings

| # | 0.28 | RTS0 before | Now |
|---|---|---|---|
| TU1 | introductory_tutorial.xml uid 101: player 1's structures/spart/gerousia at (219.7, 155.5) | The porter's branch for unnamed civ structures wrote no template, so the entry was lost | Added to the map JSON (-41.07, -57.13) |
| TU2 | Trainer.js Item.Queue raises OnTrainingQueued (Trainer.js:109-116) before ProductionQueue.AddItem pushes the batch (ProductionQueue.js:333-344); ResetQueue calls DisableAutoQueue | Raised after the push: the reset removed the wrong batch as well; autoqueue stayed on | Raised before the push; tutorial_reset_queue turns autoqueue off |
| TU3 | A gather on a remembered supply names that entity; GetResourceType reads its type | Command 51 (gather near a mirage) carried type \| subtype << 8 in target_id, read as an entity id | The type is passed and read as such (tutorial_resource_kind_of) |
| TU4 | Goal 16 counts set-rallypoint with command "gather" on grain (introductory_tutorial.js:256-259) | Any rally on a Field structure | Only a finished Field |
| TU5 | MarkPlayerAndAlliesAsWon(1, () => "Tutorial completed", () => "") (:383) | END_GAME_MODE lines | END_TUTORIAL: "Tutorial completed", no defeat line |
| TU6 | colorizeHotkey (color.js:175-190) | "#ffd966" key, "(unbound)" | hotkey_tag and "Unassigned hotkey: %(hotkeyName)s" |
| TU7 | tutorialWarning "10 100%-42 100%-20 100%-12" | -36 | -42 |
| TU8 | WarningMessage sets tutorialPanel.hidden = false (messages.js:371) | Only a changed goal or warning | Warnings counted in vars[6] above the warning's byte |
| TU9 | OnDeserialized re-sends the goal only | The saved warning showed | Cleared on load |
| TU10 | "selection.queue" | session.queue | Deliberate |
| TU11 | Every player's events | Player 1's | Kept |

## Matching

Every goal's text, hotkeys and order; each Init and IsDone; Ready and the delay -1 goal; the warnings; reload; LaunchAttack (citizen soldiers attack-walk to the first Tower, else the Civic Center, units only, no capture) and IsAttackRepelled; the enemy Civic Center's change of owner; RemoveChampions keeping 6; the walkthrough's house count and phase checks; both maps' settings, entities and resources.
