# RTS0 multiplayer vs 0 A.D. 0.28: eighteenth pass (the lobby server address, the profile's role line, a game name from the main menu)

Read-only code audit, 2026-10-21, at RTS0 `main` c24d338ce ("Merge pull request #1539"). This pass leaves out everything that `audit-028.md`, passes 2 to 5 (`/mnt/project-files/multiplayer/`), passes 6 to 17 with their fix logs (`docs/archive/`) and the multiplayer and lobby rows of `docs/REMAINING_GAPS.md` already list. It starts from the 0.28 files no earlier pass names: `ProfileButton.js`, `KickStrings.js`, `lobby.js`, `password.js`, `Subject.xml`, `ProfilePage.xml`, `GameDetails.xml`, `LeaderboardPage.xml`, `termslobby.js`, `summary/layout.js`, `counters.js`, `PersistentMatchSettings.js`, `SavedGameLabel.js`, `functions_global_object.js`, and the `[network]`, `[lobby*]`, `[gui.gamesetup]`, `[gui.session*]`, `[chat*]` and `[sound.notify]` sections of `default.cfg`.

Short paths:
- `gui/` = `/mnt/project-files/session-ui/0ad-028-public-gui/gui/`, `cfg` = `/mnt/project-files/session-ui/0ad-028-config/default.cfg`, `XC` = `src/0ad/network/lobby/XmppClient.cpp`.
- `lb` = `scenes/menu/lobby.gd`, `ls` = `scenes/menu/lobby_session.gd`, `gmp` = `scenes/menu/gamesetup_mp.gd`, `uc` = `src/presentation/user_config.cpp`.

| # | Gap (what a player sees) | 0.28 | RTS0 | Impact | Size | Save |
|---|---|---|---|---|---|---|
| 1 | The lobby page's "Lobby server" field opens on `127.0.0.1:8088` at every launch, so anyone using a lobby elsewhere retypes it each session, and until then the remembered account is sent to localhost. In 0.28 the server is the `lobby.server` config key. | `cfg:520`; `XC:94`. | `ls:25`; `lb:211`, `lb:149`; sign-in saves `lobby.login` and the password but no address (`lb:953-960`); `uc:80-106` has no `lobby.server`. | Med-Low | S | none |
| 2 | The profile panel's "Player" / "Muted Player" / "Moderator" line stays as it was when a moderator mutes, unmutes or promotes the player shown, until another player is clicked. 0.28 updates it on the role change. | `gui/lobby/LobbyPage/ProfilePanel.js:26`, `:41-45`, `:62-65`. | Set only in `_request_profile` (`lb:623`); the refresh's re-selection returns early (`lb:383-384`). | Very low | S | none |
| 3 | Host New Game or Host Saved Game from the main menu while signed in shows the Server Name row but never checks it, so two games of the same name get listed. 0.28 checks it in `startHost` (and leaving the lobby for the main menu signs out there). | `gui/gamesetup_mp/gamesetup_mp.js:417`, `:428-433`; `gui/lobby/LobbyPage/Buttons/QuitButton.js:38`. | `gmp:248-282` has no check; it exists only at `lb:1953-1957`. | Very low | S | none |

None of these touches simulation state or the save format.

## Checked, matches 0.28

- Lobby kick and ban texts with "Reason:" (`tools/lobby_server.py:314-318`, `:441`), as `KickStrings.js:59-71`.
- The profile panel's placeholders, role names, name colour and stale-answer check (`lb:615-636`, `lb:461-472`), as `ProfilePanel.js:35-124`.
- The game details text (`ls:651-730`, `lb:309-314`, `lb:1329-1331`), as `GameDetails.js:38-159`; game list colours (`lb:124-125`, `lb:1224-1225`), as `Game.js:316-335`; player list sort keys (`lb:1101-1121`), as `PlayerList.js:97-116`.
- Lobby chat formatting: own nick, chime, `/me`, `/say` (`lb:1417`, `lb:1437-1448`), as `ChatMessageFormat.js:20-71`.
- The host and join forms, their prefill and what they save (`gmp:172-195`, `gmp:58-63`, `gmp:116-118`), as `gamesetup_mp.xml:17-115` and `gamesetup_mp.js:166-184`, `:417-489`.
- Cheats in a networked game (`src/presentation/main_node.cpp:279-330`), as `Cheats.js:24-78`.
- Network warnings in the setup page (`main_node.cpp:920-990`), as `functions_global_object.js:4-33`.
- Persisted match settings and the "Saved game" label (`uc:29`, `scenes/menu/gamesetup.gd:2072`, `:2573`), as `PersistentMatchSettings.js`, `SavedGameLabel.js`.
- Config keys: every `[network]` key, `[lobby]` history, buddies, rememberpassword, terms and `columns.gamerating`, `[lobby.stun]`, `[gui.gamesetup]`, `[chat]`, `[chat.session]` and `[sound.notify]`; only `lobby.server` was missing (gap 1).
- Replays written by every networked peer (`src/presentation/match_archive.cpp:459-480`); Quit or "I will return" from a lobby game lands on the lobby page (`scenes/session/quit_confirmation.gd:171`), as `session.js:562-571`.

## Not verified

- 0.28's shared terms dialog (`openTerms`) is not in the GUI dump.
- A join with an empty "Server Hostname or IP" (`gmp:96-99`, `:252`) against ENet's resolver.
- `LobbyRatingReport/*.js`: what 0.28 sends EcheLOn (nothing on screen; LR4 is open).
- Summary team totals and charts for observers (`summary/layout.js`, `counters.js`): the Stats thread's (U14).
- The lobby game list's Players cell: 0.28 colours n/m and +N apart (`lb:1236-1239`), cosmetic.

## Fix log

- 2026-10-21 #PRn pass 18 gap 1 (the lobby signed in to is kept as `lobby.server` and opens the next launch's lobby page, as XmppClient) merged.
- 2026-10-21 #PRn pass 18 gap 2 (the profile panel's role line follows a mute, unmute or promotion, as ProfilePanel.onRoleChange) merged.
- 2026-10-21 #PRn pass 18 gap 3 (a signed-in host's Server Name from the main menu is checked against the lobby's game list, "Game name already in use.", as gamesetup_mp.js startHost) merged.
