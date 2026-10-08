# src/network

Live RTS0 netcode. The design (lockstep model, platform matrix, architecture rules) and the playtest harnesses are described in [`docs/MULTIPLAYER.md`](../../docs/MULTIPLAYER.md); remaining multiplayer work is [`docs/PLAN.md`](../../docs/PLAN.md) P1; which vendored 0 A.D. network files are ported is in [`docs/MIGRATION.md`](../../docs/MIGRATION.md).

## Files

| File | Role |
|---|---|
| `turn_manager.h/.cpp` | Lockstep `TurnManager`: 200 ms turns, command batches, sim hash after each batch, replay save/load, game-start gate. Mirrors 0 A.D. `NetClientTurnManager` / `NetServerTurnManager` in one class. |
| `net_host.h/.cpp` | `NetHost`: non-blocking UDP sockets with a small reliable-resend layer, `NMT_*` handshake, game-start and join-sync messages. Used instead of 0 A.D.'s ENet transport. |
| `net_types.h` | Wire enums and packet structs. Enum values are append-only to keep old builds compatible. |
| `lobby_client.h` | HTTP client for the room list served by `tools/lobby_server.py` (instead of 0 A.D.'s XMPP lobby). |

## Vendor boundary

`src/0ad/network/` holds 0 A.D.'s own netcode for reference. `SConstruct` skips everything under `src/0ad/`, so it is never compiled; to port a piece, adapt it into this folder under RTS0 types and update `docs/MIGRATION.md`.

## Testing

```bash
python3 tools/playtest_lockstep.py           # headless two-client lockstep (also run in CI)
python3 tools/playtest_two_clients.py --help # two real Godot clients via the lobby
```

`tools/test_netcode.cpp` (dual-sim hashes), `tools/test_replay.cpp` (replay round-trip) and `tools/test_diplo_net.cpp` (diplomacy over UDP) exercise this folder's real API.
