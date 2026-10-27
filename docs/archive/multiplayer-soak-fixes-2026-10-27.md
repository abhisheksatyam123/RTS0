# RTS0 multiplayer: fixes from the Stats thread's networked soak (2026-10-27)

The Stats thread ran `tools/soak_lockstep.py` (2-8 seats, Petra seats, observers, leaves and rejoins, pauses, 0-20% loss, save/load, each host replay checked offline) on main 8461b383f. There was no OOS between peers that stayed seated, and every replay matched. Three findings came back to the Multiplayer thread.

| # | What happened | Cause | Fix |
|---|---|---|---|
| 1 | An observer that joined about ten turns before a seated player left stalled for good, waiting on the leaver's batches. | `apply_pending_rejoin` resets the turn manager and restores the lockstep from the rejoin header (turn T). A `CMD_PEER_LEFT` (or `CMD_PEER_JOINED`) that arrived in the host's batches while the world loaded was cleared with it. | The host's join and leave notices for turns after T are applied again on top of the header (`turn_manager.cpp`). A three-player repro (the observer joins 15 turns before a leave, 10% loss) stalled in 2 of 10 runs before the fix and 0 of 5 after. |
| 2 | At 20% loss, a seated joiner sat on turn 4 while the host waited on it, every time with `--only 5 --seeds 2`. | The connect ACK is unreliable. When every copy was lost, the client never learnt its id and played as client 1, so its own batches clashed with the real client 1's. | The client takes its id from the authentication result's `host_id`, as 0.28's `NetClient::OnAuthenticate` takes `m_HostID` (`net_host.cpp`). The repro stalled in 5 of 5 runs before and 0 of 3 after. `test_net_host` case 11 drops every connect ACK. |
| 3 | A leaver can run a turn or two that the host then runs without it, so its own last hashes and replay part from the match. | A client runs a turn once it holds every batch. 0.28 waits for the server's end of turn. | Kept: only the leaver's own world differs, and the match is unaffected. |

The lockstep playtest now prints a stalled peer's waiting set and each link's reliable stream. Its observer case also saves the refused observers' output (`observer_extra.out`, `observer_stranger.out`) and prints the stranger's last line in its FAIL line.
