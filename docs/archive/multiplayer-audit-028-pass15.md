# RTS0 multiplayer vs 0 A.D. 0.28: fifteenth pass (what a setup client can open, the saved game's map, the setup's map filter, presence on the summary)

Read-only code audit, 2026-10-21, at RTS0 `main` 69fd21f5f ("Fix log: pass 14 is #1517"). This pass leaves out everything that `audit-028.md`, passes 2 to 5 (`/mnt/project-files/multiplayer/`), passes 6 to 14 with their fix logs (`docs/archive/`) and the multiplayer and lobby rows of `docs/REMAINING_GAPS.md` already list as fixed, skipped on purpose, open or checked. It starts from the 0.28 files no earlier pass names: `GameSettingControl.js`, `GameSettingsTabs.js`, `Single/Buttons/MapBrowser.js`, `MapBrowserPage.js`, `MapPreview.js`, `CivInfoButton.js`, `PerPlayer/Buttons/AIConfigButton.js`, `Controllers/GuiData.js`, `Single/Dropdowns/MapFilter.js`, `lobby/LobbyHandler.js`, `HostButton.js`, `QuitButton.js`, `ChatMessagePrivateWrapper.js`, `session/lobby/LobbyRatingReport/*`, `session/chat/ChatWindow.js`, `replaymenu/replay_filters.js`, `NetServerTurnManager.cpp` and `NetClientTurnManager.cpp`.

Short paths:
- `gui/` = `/mnt/project-files/session-ui/0ad-028-public-gui/gui/`. `GSP` = `gui/gamesetup/Pages/GameSetupPage/`. `NS` = `src/0ad/network/NetServer.cpp`, `NSTM` = `src/0ad/network/NetServerTurnManager.cpp` (vendored 0.28).
- `gs` = `scenes/menu/gamesetup.gd`, `gs.tscn` = `scenes/menu/gamesetup.tscn`, `ls` = `scenes/menu/lobby_session.gd`, `lb` = `scenes/menu/lobby.gd`, `qc` = `scenes/session/quit_confirmation.gd`, `mn` = `src/presentation/main_node.cpp`, `tm` = `src/network/turn_manager.cpp`, `tm.h` = `src/network/turn_manager.h`.

| # | Gap (what a player sees) | 0.28 | RTS0 | Impact | Size | Save |
|---|---|---|---|---|---|---|
| 1 | A setup client can't click the Map, Player or Game Type tab buttons (only the tab hotkeys open a tab), "Browse Maps", the civilization "i" button or an AI row's gear, so the read-only AI Configuration never opens. A saved game's host has the same locks on the tabs and the gear. In 0.28 everyone clicks the tabs and the viewers; a client's map browser and AI page are read only. | `GameSettingsTabs.js:29-43`, `:61-64`; `GameSettingControl.js:97-119`; `Single/Buttons/MapBrowser.js:27-35`; `MapBrowserPage.js:28-33`; `Panels/Buttons/CivInfoButton.js:9-24`; `PerPlayer/Buttons/AIConfigButton.js:8-25`. | `_apply_mp_locks` keeps only Ready, Back, Start, the chat and the host's AssignSelect cells (`gs:3105-3119`); `_lock_tree` disables every other button (`gs:3121-3125`), the tabs (`gs.tscn:66-94`), BrowseMaps (`gs:938-943`), CivInfoButton (`gs:1438-1450`) and AIConfigButton (`gs:1693-1700`) included. | Med | S | none |
| 2 | A saved game's host can still pick a new map from the preview or the map-browser hotkey; the setup then shows a map the saved world won't use. In 0.28 the preview can't be clicked, Browse Maps is hidden and its hotkey unbound for a saved game. | `GSP/Panels/MapPreview.js:16-20`; `GSP/GameSettings/Single/Buttons/MapBrowser.js:7-11`; `GameSettingControl.js:60-61`, `:84-87`, `:145`. | `gs:944-951`, `gs:974-980`, `gs:958`, `gs:962-972`; `_apply_mp_locks` leaves the preview alone (`gs:3106-3119`). | Low-Med | S | none |
| 3 | A client keeps its own Map Filter ("Default", or "All Maps"), so it can read "Default" while the host has "Naval Maps", and its map browser opens on its own filter. The host's filter is not remembered for the next setup. In 0.28 the filter travels in guiData with every setup update and is persisted with the match settings. | `gui/gamesetup/Controllers/GuiData.js:9-34`; `Controllers/GameSettingsController.js:156-159`, `:200-214`, `:259-271`; `Single/Dropdowns/MapFilter.js:17`, `:47-49`, `:65`. | `page_state` and `PERSISTED_KEYS` have no filter (`gs:2062-2066`, `gs:2103-2122`); a client copies only `PERSISTED_KEYS` (`gs:2938-2943`); `_select_map_id` keeps the client's own filter (`gs:917-930`). | Low | S | none |
| 4 | After a lobby game, others see you as Online on the summary instead of Busy, and opening and closing the lobby dialog over the summary leaves you Online. In 0.28 you stay "playing" from the join until the lobby page itself opens. | `gui/gamesetup_mp/gamesetup_mp.js:454`, `:482`, `:523`; `gui/lobby/LobbyHandler.js:16-17`; `gui/lobby/LobbyPage/Buttons/QuitButton.js:30-34`. | `_beat` posts "playing" only while the scene has `get_lobby_state` (`ls:468-486`); the summary is its own scene (`qc:155-167`); `lobby_dialog_closed` (`ls:492-500`). | Low | S | none |

None of these touches simulation state or the save format.

## Checked, matches 0.28

- **Lobby dialog buttons** (`lb:305-306`, `lb:323`, `lb:343`), as `HostButton.js:10`, `JoinButton.js:12-24`, `QuitButton.js:15-22`.
- **Read-only map browser for clients** once open (`gs:958`), as `MapBrowserPage.js:13-14`, `:30`.
- **Quicksave and quickload off in networked games** (`src/presentation/session_input.cpp:257-264`, `mn:792-793`), as `gui/session/hotkeys/misc.xml:19-25`.
- **Developer overlay only with cheats in a networked game** (`mn:1713-1720`), as `DeveloperOverlay.js:37-41`.
- **In-match Summary player filter** (`src/presentation/match_archive.cpp:327-336`), as `gui/session/MenuButtons.js:100-114`.
- **Replay filters** (`scenes/menu/replay_browser.gd:38-40`), as `gui/replaymenu/replay_filters.js:152-186`.
- **A client's last setup slot** (`mn:3078-3088`), as `PlayerAssignmentsController.js:116-148`.
- **Session chat History setting** (`src/presentation/user_config.cpp:143`, `mn:1423`), as `gui/session/chat/ChatWindow.js:30-36`.
- **Networking and Lobby options** (`scenes/menu/options.gd:103` and the others), as `gui/options/options.json:539-591`.
- **Command delay of 4 turns** (`tm.h:101-106`), as `NSTM:43-50`.
- **Disconnect texts** for reasons 2, 3, 5-10, 14 and 17 (`mn:994-1006`), as `gui/common/network.js:64-80`.

## Not verified

- Joining while the game loads: 0.28 refuses with reason 4, "Game is loading, please try again later." (`NS:982-987`, `NS:1595-1609`, `network.js:69`); RTS0 declines a join request with reason 2 while `current_turn_ == 0` (`tm:542`, `mn:4004-4009`). Not traced or run.
- Moderator private messages in the lobby (`ChatMessageEvents.js:20-26`, `ChatMessagePrivateWrapper.js:23-32`): RTS0's server has no private-message path, and 0.28's GUI can't send one either.
- Pass 14's `GuiData.js:66-95` citation points past the end of the 35-line file; gap 3 cites the right lines.

## Fix log

- 2026-10-21 #PR1 pass 15 gap 1 (the tabs, Browse Maps, the civ info button and the AI gear stay clickable for a client and a saved game's host; the map browser and AI Configuration open read only, as GameSettingsTabs, CivInfoButton, MapBrowser.js and AIConfigButton) merged.
- 2026-10-21 #PR1 pass 15 gap 2 (a saved game hides Browse Maps and opens no map browser from the preview or the hotkey, as MapPreview.js and MapBrowser.js EnabledWhenSavedGame) merged.
- 2026-10-21 #PR1 pass 15 gap 3 (the host's Map Filter travels with the setup as `mapfilter` and is persisted with the match settings; clients show it, as GuiData.js) merged.
- 2026-10-21 #PR1 pass 15 gap 4 (lobby presence stays "playing" from the game until the lobby page itself opens, the summary and a closed lobby dialog included, as gamesetup_mp.js and LobbyHandler) merged.
