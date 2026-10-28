# RTS0 multiplayer vs 0 A.D. 0.28: twenty-first pass (a game waiting for a rejoin)

Read-only code audit, 2026-10-28, at RTS0 `main` c0b327184. This pass leaves out everything that `audit-028.md`, passes 2 to 5 (`/mnt/project-files/multiplayer/`), passes 6 to 20 with their fix logs, the soak fixes (`docs/archive/multiplayer-soak-fixes-2026-10-27.md`) and the multiplayer and lobby rows of `docs/REMAINING_GAPS.md` already list. It sweeps the vendored 0.28 `NetServer`, `NetClient`, `NetServerTurnManager`, `NetClientTurnManager`, `NetFileTransfer`, `XmppClient` and the prelobby pages for behaviour no earlier pass names. Only the lobby's game states were left.

Short paths: `gui/` = `/mnt/project-files/session-ui/0ad-028-public-gui/gui/`, `lb` = `scenes/menu/lobby.gd`, `ls` = `scenes/menu/lobby_session.gd`, `srv` = `tools/lobby_server.py`.

| # | Gap (what a player sees) | 0.28 | RTS0 | Impact | Size | Save |
|---|---|---|---|---|---|---|
| 1 | A running lobby game that has lost a player still reads as running (red). In 0.28 the lobby bot lists it as "waiting" (orange) until the player is back, so others can see that it is waiting for a rejoin. Selecting such a game also left Observe unticked. | `gui/lobby/LobbyPage/Game.js:303-327` (init, waiting, running and their colours); the lobby bot's `change_game_state` compares the game's player count with the one it registered with (`nbp-init`). `LobbyGamelistReporter.js:23` reports the count. | `ls:548-549` posts the seated players and "running"; `srv` accepted only "init" and "running"; `lb:125-126` already draws "waiting". Observe followed `== "running"` (`lb:1792`). | Low | S | none |

None of these touches simulation state or the save format.

## Checked, matches 0.28

- Wrong-password limits per name (`NetServer.cpp:1753-1772`), as `failed_passwords_` in `src/network/net_host.h:341-344`.
- The connect flow's id: since #1634 the client takes its id from the authentication result, as `NetClient` takes `m_HostID`.
- XmppClient's error and registration texts: what RTS0's lobby can produce has its 0.28 wording. The rest (TLS, stream, XMPP stanza errors) belong to an XMPP server that RTS0's HTTP lobby does not have.
- The prelobby's login, register, remember-password and terms flow (`gui/prelobby/`), as passes 4, 5 and 14 found.
- The game's seated player count for the lobby (`src/presentation/session_lobby.cpp:112-129`) leaves out observers, as `nbp` does.

## Not verified

- The lobby bot is not vendored. Its `change_game_state` is taken from the 0 A.D. lobby-bots source as remembered, not from a copy in the shared folder.
- Carried over: `openTerms`, LR4, U14.

## Fix log

- 2026-10-28 #1710 pass 21 gap 1 (a running lobby game short of a player is listed as waiting, and Observe is ticked for it, as the 0.28 lobby bot and JoinButton) merged.
