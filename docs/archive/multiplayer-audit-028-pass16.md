# RTS0 multiplayer vs 0 A.D. 0.28: sixteenth pass (joining while the game loads, a winner who stays, typed chat tags, the player view's colours)

Read-only code audit, 2026-10-21, at RTS0 `main` a6fdbe10b. This pass leaves out everything that `audit-028.md`, passes 2 to 5 (`/mnt/project-files/multiplayer/`), passes 6 to 15 with their fix logs (`docs/archive/`) and the multiplayer and lobby rows of `docs/REMAINING_GAPS.md` already list. It starts from the 0.28 files no earlier pass names: `session/TimeNotificationOverlay.js`, `SessionMessageBox.js`, `TopPanel.js`, `chat/ChatMessageHandler.js`, `diplomacy/*`, `lobby/LobbyService.js`, `match_settings/MatchSettingsDialog.js`, `minimap/MiniMapFlareButton.js`, `replaymenu/replay_actions.js`, `gamesetup/NetMessages.js`, `Panels/Chat/ChatPanel.js`, `GameDescription.js`, `TipsPanel.js`, `lobby/LobbyPage/Chat/ChatPanel.js`, `LeaderboardButton.js`, `loading/loading.js`, `NetFileTransfer.cpp`, `NetStats.cpp`, `NetMessages.h` and `NetServer.h`. Gap 1 is pass 15's "Not verified" join while loading, traced for this pass.

Short paths:
- `S/` = `/mnt/project-files/session-ui/0ad-028-public-gui/gui/session/`. `NS` = `src/0ad/network/NetServer.cpp` (vendored 0.28).
- `mn` = `src/presentation/main_node.cpp`, `mh` = `main_node.h`, `cw` = `src/presentation/chat_window.h`, `sl` = `src/presentation/session_lifecycle.cpp`, `cr` = `src/network/chat_routing.h`, `tm` = `src/network/turn_manager.cpp`, `nh` = `src/network/net_host.cpp`, `ma` = `src/presentation/match_archive.cpp`, `si` = `src/presentation/session_input.cpp`, `sp` = `src/presentation/session_pointer.cpp`.

| # | Gap (what a player sees) | 0.28 | RTS0 | Impact | Size | Save |
|---|---|---|---|---|---|---|
| 1 | A client that joins after Start but before the first turn sits on "Registering with server..." for good: the host turns its rejoin request down as "still in its lobby" and never sends it a game start. In 0.28 the host refuses it at once with "Game is loading, please try again later." | `NS:982-987` (OnAuthenticate, SERVER_STATE_LOADING), `NS:1595-1609` (StartGame drops a client not yet authenticated), `gui/common/network.js:69`. | `serve_rejoin` rejects with reason 2 while `current_turn_ == 0` (`tm:542`); the client drops the request and waits (`tm:388-393`); `nh` authenticates regardless of the game's state. | Med | S | none |
| 2 | A player who has won and pressed Stay can't order their units: right-click, the minimap and the command panel all stop. In 0.28 only "defeated" takes control away (`controlsPlayer`), so a winner plays on while viewing their own side; the diplomacy rows stay closed to them (`isPlayerObserver`). | `S/session.js:456-474`; `S/input.js:867`, `:1183`; `S/minimap/MiniMap.js:62`; `S/selection_panels.js` `controlsPlayer(data.player)`; `S/diplomacy/DiplomacyPlayerControl.js:23-28`. | `spectating()` counts a winner (`mn:1444-1448`) and gates every order (`mn:3722`, `si:847`, `si:1636`, `si:1675`, `sp:651`) and the command panel (`command_queue_view.h:316-330`); the lockstep drops only defeated players (`tm:1670`). | Low-Med | S | none |
| 3 | Typed text that starts with a wire tag is rerouted: "[Player-3] hi" sent to everyone reaches player 3 only, "[Allies] x" the allies, and "[Flare] 100,200" pings the map for allies and observers. In 0.28 the text goes as typed; only the slash commands pick addressees and flares are their own message. | `S/chat/Chat.js:83-98`; `S/chat/ChatMessageFormatPlayer.js:72-144`. | `send_pressed` sends `wire_tag + text` with no escape (`cw:367-418`); the host routes by `chat_wire_tag` (`cr:14-28`) and the receivers parse it (`cw:424-465`, `sl:107-138`, the flare check ahead of the setup chat's verbatim path). | Low | S | none |
| 4 | The player view's "■" squares keep the first player colours, so with Diplomacy Colors on they no longer match the units and the minimap. 0.28 colours them with `colorizePlayernameHelper` and rebuilds the list on a view or Diplomacy Colors change. | `S/top_panel/PlayerViewControl.js:24`, `:42-50`; `S/session.js` `registerDiplomacyColorsChangeHandler`. | `new_player_view_picker` fills them once from `player_color` (`ma:636-653`). | Low | S | none |
| 5 | A client without a name reads "peer-N" in the join, leave and rejoin lines, and the flare rate limit names the sender by GUID. 0.28 says "Unknown Player" and names the player. | `S/chat/ChatMessageFormatNetwork.js:16-77`; `S/messages.js:518-521`, `:540-551`. | `post_network_chat` `who()` (`mn:1088-1094`); `sl:123-124`. | Very low | S | none |

None of these touches simulation state or the save format: gap 2 lets through only commands the lockstep already accepts.

## Checked, matches 0.28

- **Rejoin and late observers**: a known name gets its seat back, observerlimit and lateobservers (`tm:534-606`), as `NS:1056-1120`; leavers are not defeated (`tm:221-235`).
- **Disconnect overlay** texts, titles and Exit to the summary (`mn:982-1085`), as `S/NetworkStatusOverlay.js:12-45` and `network.js:61-88`.
- **Pause overlay and who may pause** (`mn:842-899`), as `PauseOverlay.js:29-42`, `PauseControl.js:111-149`; no implicit pause in multiplayer for the menu, message boxes, summary or tech tree, as `SessionMessageBox.js:7-27`.
- **Quit, resign and defeat boxes** (`scenes/session/quit_confirmation.gd:57-178`), as `QuitConfirmation*.js`, `ResignConfirmation.js` and `session.js` endGame.
- **Diplomacy dialog** gating by control, ceasefire and locked teams (`src/presentation/diplomacy_dialog.h:313-436`), as `StanceButton.js`, `TributeButton.js`, `AttackRequestButton.js`.
- **Attack requests** to allies (`simulation.cpp:30290-30302`), as `Commands.js:780-796`.
- **Network delay overlay** (`sl:1569-1588`), as `NetworkDelayOverlay.js:66-109`.
- **Flares**: tooltips, cursor, no flares in a replay, 3 s send cooldown, 16 in 6 s receive limit and routing (`cr:61`), as `MiniMapFlareButton.js`, `input.js:1290-1301`, `messages.js:507-529`.
- **Session chat**: 1024 characters, Tab completion, private targets, team and private hotkeys, the host's GUID stamp, as `ChatInput.js`, `ChatAddressees.js`, `Chat.js`.
- **AI chat**, **tribute, barter, phase and diplomacy notices**, **Match Settings player states**, **rated-game line**, **lobby Join**, **Follow Player**.

## Not verified

- A slot kept for dropped players against MAX_CLIENTS (`NS:1110-1113`): not traced through RTS0's accept path.
- Chat that arrives during a rejoin download, which 0.28 drops (`gamesetup_mp.js:299-300`).
- Rejoin transfer throughput (0.28 `NetFileTransfer.h:39-48` vs `REJOIN_CHUNKS_PER_TICK`).
- The NetStats profiler table (developer only) and the lobby's "waiting" game state (the 0.28 lobby bot is not vendored).

## Fix log

- 2026-10-21 #1530 pass 16 gap 1 (a join while the started game loads is refused with reason 4, "Game is loading, please try again later.", as NetServer OnAuthenticate and StartGame) merged.
- 2026-10-21 #1530 pass 16 gap 2 (a winner who stays orders their own units while viewing their own side, as controlsPlayer(g_ViewedPlayer); the diplomacy rows stay closed) merged.
- 2026-10-21 #1530 pass 16 gap 3 (a line to everyone that starts with "[" travels behind "[All] ", so typed tags and "[Flare]" stay text; the setup chat makes no flare) merged.
- 2026-10-21 #1530 pass 16 gap 4 (the player view's squares follow Diplomacy Colors) merged.
- 2026-10-21 #1530 pass 16 gap 5 ("Unknown Player" for a nameless client's join and leave lines; the flare limit names the player) merged.
