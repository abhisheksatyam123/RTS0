# RTS0 multiplayer vs 0 A.D. 0.28: fourteenth pass (the sign-in name and remembered password, lobby chat line formats, name colours in setup and match chat, the lobby dialog's frame, the leaderboard list)

Read-only code audit, 2026-10-21, at RTS0 `origin/main` 9890f4a74 (#1511). This pass leaves out everything that `audit-028.md`, passes 2 to 5 (`/mnt/project-files/multiplayer/`), passes 6 to 13 with their fix logs (`docs/archive/`) and the multiplayer and lobby rows of `docs/REMAINING_GAPS.md` already list as fixed, skipped on purpose, open or checked. It starts from the 0.28 files no earlier pass names: `prelobby/entrance`, `login`, `register`, `credentials`; `lobby/LobbyPage` ChatPanel, ChatMessageFormatSay, SystemMessageFormat, BuddyButton, LeaderboardButton, LobbyPage; the setup's GuiData, NetMessages and ChatPanel; the session's LobbyService, LobbyRatingReport, ChatMessageHandler, OutOfSyncReplay and QuitConfirmationReplay.

Short paths:
- `gui/` = `/mnt/project-files/session-ui/0ad-028-public-gui/gui/`. `XC` = `src/0ad/network/lobby/XmppClient.cpp` (vendored 0.28).
- `mn` = `src/presentation/main_node.cpp`, `cw` = `src/presentation/chat_window.h`, `dc` = `src/presentation/display_colors.h`, `lb` = `scenes/menu/lobby.gd`, `ls` = `scenes/menu/lobby_session.gd`, `gmp` = `scenes/menu/gamesetup_mp.gd`, `srv` = `tools/lobby_server.py`, `qc` = `scenes/session/quit_confirmation.gd`, `rb` = `scenes/menu/replay_browser.gd`.

| # | Gap (what a player sees) | 0.28 | RTS0 | Impact | Size | Save |
|---|---|---|---|---|---|---|
| 1 | A returning lobby player finds their multiplayer name (Options, their last IP game, or the computer's user name) in the lobby's Player name field instead of the account they last signed in with, and an empty Password field. The remembered password counts only when the name equals `lobby.login`, so Sign In usually signs in as a guest, fails with "Wrong username or password", or makes them type both again. Signing in never saves the account as the multiplayer name, and "Remember password" is saved only after a successful sign-in. In 0.28 the login page opens with `lobby.login` and the remembered password (masked), each login saves the name as `playername.multiplayer`, and the box is saved when clicked. | `prelobby/entrance/entrance.js:3-4`, `prelobby/login/login.js:8-9`, `:50`, `prelobby/common/credentials/credentials.js:47-66`, `:69-80`, `:82-95`, `prelobby/register/register.js:38`. | `lb:197-198` then `lb:150`, `lb:1761-1765` overwrite the name; `lb:203-205`; `lb:889-896`; guest sign-in `ls:258-276`, `srv:431-439`; `lb:916-924` saves no `playername.multiplayer`; the toggle saves nothing (`lb:210-214`, `lb:717-735`). | Med | S | none |
| 2 | Lobby chat: a player's line reads "nick: text" with a plain name, where 0.28 has "<nick> text" with the name in bold. Join, leave, role and subject lines are plain grey, where 0.28 has "== X has joined." in bold. "Connected.", "Disconnected.", your own kick and command errors are plain grey, where 0.28 has "=== System: …" in bold with a red "System:", and the disconnect line names its reason. The join, leave and subject lines are English text from the lobby server. | `lobby/LobbyPage/Chat/ChatMessages/ChatMessageFormatSay.js:29-38`, `ChatMessageFormat.js:33-66`, `StatusMessageFormat.js:23-29`, `SystemMessageFormat.js:23-36`, `StatusMessageEvents.js:26-66`, `SystemMessageEvents.js:21-56`, `ChatCommandHandler.js:29-69`. | `lb:1397-1407`, `lb:1655-1656`, `lb:708`, `lb:761`, `lb:1671-1689`, `lb:1420-1427`; `srv:277`, `srv:380-381`, `srv:451`, `srv:458`. Compare `cw:614-620`. | Low-Med | S | none |
| 3 | Setup chat: a seated player's name keeps the raw slot colour (the default blue, purple or black are hard to read on the dark panel), the sender is not bold, and a nameless client shows as "peer-N". In 0.28 setup names are lightened to at least 60% lightness, the sender tag is bold, and a missing name reads "Unknown Player"; the same goes for the ready and leave lines. | `gamesetup/NetMessages/ClientChat.js:24-54`, `ClientReady.js:22-23`, `ClientConnection.js:21`. | `mn:3339-3346` (`setup_client_name`), used at `mn:3360`, `mn:3368-3370`, `mn:4134-4135`; `cw:427`, `cw:474`, `cw:614-620`. | Low | S | none |
| 4 | Match chat: an observer's name is grey (#b3b3b3) on chat lines, in `/list` and in connection warnings, where 0.28 has white. With Diplomacy Colors on, player names there keep their own colours, where 0.28 uses the diplomacy colours. A nameless sender shows as "peer-N" instead of "Unknown Player". | `gui/session/messages.js:540-551`, `gui/session/DiplomacyColors.js:73-76`. | `mn:4131-4152` (`chat_user`: "b3b3b3", `sim->player_color`, `sender_guid`), used at `cw:406`, `cw:462`, `mn:410-426`, `mn:932`; flares and the waiting list already use white and `display_colors::rgb` (`mn:503`, `mn:1055-1058`). | Low | S | none |
| 5 | The lobby opened as a dialog over the setup, the match or the summary covers the screen with an opaque page. In 0.28 it is a framed window 40 px in from each edge over a translucent fade. | `lobby/LobbyPage/LobbyPage.js:46-73`, `LobbyPage.xml:8`. | `lb:84-99`, `lb:164-175`. | Low | S | none |
| 6 | The Leaderboard opens empty and fills when the server answers; Update empties it and drops the selection. 0.28 shows the last list at once while it asks again, and keeps the selection. | `lobby/LeaderboardPage/LeaderboardPage.js:29-42`, `LeaderboardList.js:40-63`. | `lb:616-622`, `lb:625-637`, `lb:445-448`. | Low | S | none |

None of these touches simulation state or the save format.

## Checked, matches 0.28

- **Lobby "/me" and announcements** (`lb:1403-1404`, `lb:1428-1436`), as `ChatMessageFormatMe.js:24-31`, `ChatMessageFormatAnnouncement.js:24-37`.
- **Lobby chat timestamps** (`lb:1462-1467`), as the lobby's `TimestampWrapper.js:25-33`.
- **Role and subject texts** (`lb:1412-1418`, `srv:380-381`), as `StatusMessageEvents.js`. Only their format differs (gap 2).
- **Buddy button** (`lb:1256-1275`), as `BuddyButton.js:32-70`.
- **Account settings** (`lb:496-559`), as `AccountSettingsPage.xml:12-31`, `AccountSettingsPage.js:49-101`.
- **Replay end and replay out-of-sync boxes** (`qc:19-26`), as `QuitConfirmationReplay.js:42-65`, `OutOfSyncReplay.js:20-29`.
- **Replay player filter** (`rb:571-580`), as `replay_menu.js:112-124`.
- **Summary caption for a seatless client** (`scenes/session/summary_screen.gd:573-589`), as `summary.js:511-523`.
- **Joining your own running game from the lobby** (`lb:1816-1817`, `src/network/turn_manager.cpp:561-588`), as `JoinButton.js:57-68`.
- **Lobby presence while hosting or joining** (`ls:474-498`), as `gamesetup_mp.js:132`, `:523`, `LobbyHandler.js:16-17`.

## Not verified

- "waiting" games in the lobby list (`XC:501-503`, `gui/lobby/LobbyPage/Game.js:303-334`): the 0.28 lobby bot's rule is not vendored.
- Rating announcements from the 0.28 lobby bot.
- Whether a setup client's map filter can differ from the host's (`gamesetup/Controllers/GuiData.js:66-95`, `scenes/menu/gamesetup.gd:915-917`).
- Register then sign in: RTS0 signs in at once on purpose (`lb:950-951`), where 0.28 opens the login page (`register.js:36-44`).

## Fix log

- 2026-10-21 #PR1 pass 14 gap 1 (the lobby opens with the account last signed in and the remembered password, masked, which Sign In sends while it stays; a sign-in saves the account as the multiplayer name; Remember password is saved when clicked, as login.js and credentials.js) merged.
- 2026-10-21 #PR1 pass 14 gap 2 (lobby chat reads "<nick> text" with the sender in bold, "== …" in bold for the lobby's join, leave, role and subject lines, and "=== System: …" in bold with a red "System:" for Connected, Disconnected, your own kick, /help and command errors, as ChatMessageFormatSay, StatusMessageFormat and SystemMessageFormat; the disconnect reason and translatable join and leave lines stay open, as the lobby server words those) merged.
- 2026-10-21 #PR1 pass 14 gap 3 (setup chat names lightened to at least 60% lightness, the sender tag in bold, "Unknown Player" for a nameless client, as ClientChat.js) merged.
- 2026-10-21 #PR1 pass 14 gap 4 (match chat, /list and connection warnings name an observer in white and a player in the Diplomacy Colors colour, "Unknown Player" without a name, as colorizePlayernameByGUID) merged.
- 2026-10-21 #PR1 pass 14 gap 5 (the lobby as a dialog is a window 40 px in from each edge over a translucent fade, as LobbyPage.setDialogStyle) merged.
- 2026-10-21 #PR1 pass 14 gap 6 (the Leaderboard shows the last list at once and Update keeps the selected row, as LeaderboardPage and LeaderboardList.rebuild) merged.
