# RTS0 multiplayer vs 0 A.D. 0.28: nineteenth pass (an empty join address, the mods' tooltips, the order of the join checks)

Read-only code audit, 2026-10-21, at RTS0 `main` 344d2177b ("Merge pull request #1548"). This pass leaves out everything that `audit-028.md`, passes 2 to 5 (`/mnt/project-files/multiplayer/`), passes 6 to 18 with their fix logs (`docs/archive/`) and the multiplayer and lobby rows of `docs/REMAINING_GAPS.md` already list. It starts from pass 18's "Not verified" (the lobby's Players cell, an empty join address), then sweeps a normal lobby game end to end. Every translated string in 0.28's lobby, gamesetup_mp, gamesetup, session chat, messages, message boxes, summary and network.js is present in RTS0, but for format strings and the XMPP-only texts pass 7 skipped: this area is close to done.

Short paths: `gui/` = `/mnt/project-files/session-ui/0ad-028-public-gui/gui/`, `NS` = `src/0ad/network/NetServer.cpp`, `gmp` = `scenes/menu/gamesetup_mp.gd`, `lb` = `scenes/menu/lobby.gd`, `ls` = `scenes/menu/lobby_session.gd`, `mn` = `src/presentation/main_node.cpp`, `nh` = `src/network/net_host.cpp`.

| # | Gap (what a player sees) | 0.28 | RTS0 | Impact | Size | Save |
|---|---|---|---|---|---|---|
| 1 | Joining with an empty "Server Hostname or IP" connects to 127.0.0.1, then shows the player's own game or the timeout box, and saves the blank address. 0.28 shows "Cannot join game: Failed to connect to server." (`enet_address_set_host("")` fails on Linux and macOS), stays on the join page and saves nothing. | `gui/gamesetup_mp/gamesetup_mp.js:150-160`, `:462-476`; `src/0ad/network/NetSession.cpp:74-78`. | `gmp:83-85` passes ""; `gmp:64-67` saves it; `mn:4060` turns it into 127.0.0.1. | Low | S | none |
| 2 | The lobby game list's Players cell is one colour; 0.28 draws n and m in 0 160 160 and "/", " +" and the observer count in 0 128 128. | `gui/lobby/LobbyPage/Game.js:260-275`, `:340-349`. | `lb:1190-1192`, `lb:1240-1243`. | Very low | S-M (a Tree cell holds one colour) | none |
| 3 | The game details' red "Mods (incompatible):" heading has no tooltip listing the differences, and the grey mods lack "This mod does not affect MP compatibility". | `gui/lobby/LobbyPage/GameDetails.js:100-120`. | `ls:742-752`. | Very low | S | none |
| 4 | A direct join with a name in use and a wrong password reads "Player name in use…"; 0.28 checks the password first. RTS0 also renames a duplicate behind lobby authentication with `network.duplicateplayernames` on, which 0.28 does only without it. | `NS:1005-1033`. | `nh:1397-1406` before `nh:1413-1424`. | Negligible | S | none |

None of these touches simulation state or the save format.

## Checked, matches 0.28

- The host port's range message for a blank or bad port, and the join port's default (`gmp:269-272`, `gmp:46-48`), as `gamesetup_mp.js:175-184` and `network.js:50-56`.
- Connection captions, disconnect reasons, the "Connection Error" box with Open FAQ, the "Disconnected" title and the in-match overlay (`mn:1005-1048`), as `gamesetup_mp.js:205`, `:331`, `:366` and `network.js:61-160`.
- NetworkStatusOverlay's waiting and synchronizing lines (`mn:1053-1078`), as `NetworkStatusOverlay.js:36-45`.
- The setup chat's ready lines and its Send button and Tab tooltip (`mn:3459-3482`, `scenes/menu/gamesetup.gd:2643-2654`), as `ClientReady.js` and `ChatPanel.xml`.
- The summary headline for each ending and its Watch Replay and Lobby buttons (`scenes/session/summary_screen.gd:573-589`, `:765-801`), as `summary.js:508-555`.
- Game list sort keys and double-click to join (`lb:1144-1160`, `lb:306`), as `Game.js:117-146` and `JoinButton.js:16`.
- No game speed button in a networked game (`mn:1593`).

## Not verified

- Windows ENet resolves an empty host to the local host, so there 0.28 may behave as RTS0 did for gap 1.
- Carried over: `openTerms`, the LobbyRatingReport contents (LR4), the summary's team totals and charts for observers (U14).

## Fix log

- 2026-10-21 #PRn pass 19 gap 1 (an empty join address is refused with "Cannot join game: Failed to connect to server." and nothing is saved) merged.
- 2026-10-21 pass 19 gap 2 kept: cosmetic, and a Tree cell takes a single colour.
- 2026-10-21 #PRn pass 19 gap 3 (the incompatible-mods heading lists the differences on hover, and the grey mods say "This mod does not affect MP compatibility", as GameDetails.js) merged.
- 2026-10-21 #PRn pass 19 gap 4 (the password is checked before the name, and a duplicate is renamed only without lobby authentication, as NetServer OnAuthenticate) merged.
