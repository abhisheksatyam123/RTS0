# RTS0 multiplayer vs 0 A.D. 0.28: twentieth pass (replays after a rejoin, a saved game's seats, the out-of-sync box on a client)

Read-only code audit, 2026-10-21, at RTS0 `main` 3bed76a86 ("Merge pull request #1555"). This pass leaves out everything that `audit-028.md`, passes 2 to 5 (`/mnt/project-files/multiplayer/`), passes 6 to 19 with their fix logs (`docs/archive/`) and the multiplayer and lobby rows of `docs/REMAINING_GAPS.md` already list. Pass 19 found the lobby and setup texts complete, so this pass follows a networked match itself: what each peer records, what a replay of it does, and what a client sees when it goes out of sync.

Short paths: `NCTM` = `src/0ad/network/NetClientTurnManager.cpp`, `NC` = `src/0ad/network/NetClient.cpp` (vendored 0.28), `tm` = `src/network/turn_manager.cpp`, `tm.h` = `turn_manager.h`, `ma` = `src/presentation/match_archive.cpp`, `sl` = `src/presentation/session_lifecycle.cpp`.

| # | Gap (what a player sees) | 0.28 | RTS0 | Impact | Size | Save |
|---|---|---|---|---|---|---|
| 1 | The replay of a networked game in which someone rejoined goes wrong from the rejoin on: the rejoiner's new client id has no seat in playback, so its orders are dropped and the replay drifts from the match. A saved game continued in multiplayer replays every client as its first-match seat. 0.28 records each command with its player (`m_Player`), so its replays play any rejoin and any seating. | `NCTM:125-129` (`AddCommand(client, player, …)`); the replay logger writes the player with each command. | Live play seats a rejoiner by the host's `CMD_PEER_JOINED` (`apply_peer_control`); replay playback skips that (`tm:1669`, `player_for_client`); `host_start_saved_game` seats clients (`ma:913`) but the replay meta keeps no seats. | Med | S | none |
| 2 | A client that goes out of sync opens the "Out of Sync" box as soon as its own hash differs, before the host's report, so the box names no players and no host dump, and a second box follows when the report comes. 0.28 opens it only on the host's `NMT_SYNC_ERROR`. | `NC:1037`; `NCTM:132-190` (the "out-of-sync" message carries the turn, hashes and player names). | `sl:1023-1026` opens on `has_out_of_sync()`, set on the client's own check. | Low | S | none |

None of these touches simulation state or the save format: a replay file gains an optional `seats` entry in its metadata, and older replays play as before.

## Checked, matches 0.28

- Every networked peer, observers included, writes a replay with the match settings and commands (`ma:459-480`).
- The host's hash check names the out-of-sync players and both peers write a dump (`tm` OOS handling), as `NetServerTurnManager.cpp` and `NCTM:132-190`.
- A rejoiner's commands in the live match go to its seat after the host's `CMD_PEER_JOINED`, and defeated players' commands are dropped (`tm:1669-1671`), as 0.28's per-command player.

## Not verified

- A replay of a long networked match with several rejoins (needs a recorded match).
- Carried over: `openTerms`, the LobbyRatingReport contents (LR4), the summary's team totals and charts for observers (U14).

## Fix log
