# RTS0 multiplayer vs 0 A.D. 0.28: seventeenth pass (rejoin pacing, chat while rejoining, the session limit)

Read-only code audit, 2026-10-21, at RTS0 `main` a2e5ead89 ("Merge pull request #1530"). This pass leaves out everything that `audit-028.md`, passes 2 to 5 (`/mnt/project-files/multiplayer/`), passes 6 to 16 with their fix logs (`docs/archive/`) and the multiplayer and lobby rows of `docs/REMAINING_GAPS.md` already list. It starts from pass 16's "Not verified" list: the slot 0.28 keeps for dropped players, chat during a rejoin download, and rejoin throughput.

Short paths:
- `NS` = `src/0ad/network/NetServer.cpp`, `NC` = `NetClient.cpp`, `NFT` = `NetFileTransfer.cpp` / `.h` (vendored 0.28). `gmp.js` = `gui/gamesetup_mp/gamesetup_mp.js`.
- `nh` = `src/network/net_host.cpp`, `tm` = `src/network/turn_manager.cpp`, `tm.h` = `turn_manager.h`, `sl` = `src/presentation/session_lifecycle.cpp`, `cw` = `src/presentation/chat_window.h`.

| # | Gap (what a player sees) | 0.28 | RTS0 | Impact | Size | Save |
|---|---|---|---|---|---|---|
| 1 | On a slow or lossy host connection the rejoin box stays up far longer than it needs to: the host sends 16 world chunks every frame whatever the link takes, and resends every unacknowledged one, so resends pile up. 0.28 keeps at most 32 packets in flight and sends more only as acks come back. 0.28 also zlib-compresses the state. | `NFT.h:37-48` (1024-byte packets, window 32); `NFT:177-196`; `NC:596-604` (CompressZLib). | `tm:1500-1506` and `tm.h:543-549` (`REJOIN_CHUNKS_PER_TICK`, no window); `nh:1141-1160` (resends each pending packet); `tm:671-694` (raw `save_state` bytes). | Low-Med | S (window), M (compression) | none |
| 2 | A rejoiner can hear the name chime during the world download: chat that arrives then is logged behind the rejoining page and plays `sound.notify.nick` if it names them, before the log is cleared at load. 0.28 drops chat while rejoining. | `gmp.js:252`, `:299-300` (`case "chat": break;` while `g_IsRejoining`). | `sl:107-158` (onChat, `sound_notification_nick` at `:156`); `cw:429-436`; cleared at `sl:931`. | Very low | S | none |
| 3 | The host lets in two more clients than 0.28: 41 remote clients plus itself. 0.28's 41 sessions hold the host's own client and the joiner (a session is added on connect, before OnAuthenticate checks `>= MAX_CLIENTS`), so 39 others get in. | `NS:78`, `NS:522-524`, `NS:1046-1049`. | `nh:692-700` (connected peers only); `net_types.h:302-304`. | Very low | S | none |

None of these touches simulation state or the save format.

## Pass 16's "Not verified", answered

- **The slot for dropped players** (`NS:1110-1113`): RTS0 has no `sessions + disconnectedPlayers` clause, but in 0.28 it applies only to observers already within `observerlimit` (at most 32), and with 8 players the 41 cap is never reached that way. RTS0's observer check (`tm:605`) matches. The only real difference is gap 3.
- **Chat during the rejoin download**: its text never shows (behind the opaque page, cleared at load), as 0.28; only the chime differs (gap 2). Chat after the world loads, while the game catches up, is kept, as 0.28 keeps chat queued after `start` (`NC:824-837`, `gmp.js:289-297`).
- **Rejoin throughput**: gap 1.

## Checked, matches 0.28

- Observer limit and late observers on a running game (`tm:578-606`), as `NS:1056-1118`.
- The world still streams while paused (`tm:1500` before the pause check), and a stalled transfer is asked for again after 5 s (`tm:749-750`).
- Lobby game-list filters (`scenes/menu/lobby.gd:264-288`, `:1259-1280`), as `lobby/LobbyPage/GameListFilters/*.js`.
- Reset Civs and Reset Teams hidden for clients, scenarios and saved games (`scenes/menu/gamesetup.gd:1379-1413`), as `ResetCivsButton.js`.
- Observer max lag (`tm:254`), as `NetServerTurnManager.cpp:83`.
- Diplomacy and Trade buttons and the civ emblem with no player viewed (`src/presentation/top_panel.h:226`, `:361`); spy requests (`src/presentation/diplomacy_dialog.h:222`, `:296-304`).

## Not verified

- Real rejoin world sizes and transfer times on a large map and a slow uplink (needs a run).
- STUN: `StunClient.cpp` is not vendored.
- Whether a host that observes counts toward the observer limit (0.28 counts it, `NS:1110-1111`).
- Carried over: the NetStats table, the lobby's "waiting" state (the 0.28 lobby bot is not vendored).

## Fix log

- 2026-10-21 pass 17 gap 1 kept: a window of 32 unacknowledged packets per rejoiner (NetFileTransfer's) was tried and failed `playtest_lockstep.py`'s heavy-loss observer case (45% of datagrams lost: the world did not arrive within 30 s), because RTS0 resends each packet on its own backoff and the turn batches to the rejoiner share the window, unlike ENet. RTS0 keeps 16 chunks a frame; compression is left out too (no zlib in RTS0's network layer).
- 2026-10-21 #1539 pass 17 gap 2 (a rejoiner drops chat while the world downloads, as gamesetup_mp.js) merged.
- 2026-10-21 #1539 pass 17 gap 3 (the host takes 39 remote clients, as 0.28's MAX_CLIENTS sessions) merged.
