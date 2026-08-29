# RTS0 Network Layer — Vendor vs Active, ENet Plan & Testing

> **2026-08-29 Audit:** `src/network/*` is currently a **single lockstep stub** (`turn_manager.cpp:183` LOC, `net_host.cpp:9` no-op). 0AD reference is vendored under `src/0ad/network/*` (not built). See `docs/MULTIPLAYER.md:2026-08-29 Gap Detail (G8-G9)` and `docs/REMAINING_GAPS.md` G8-G9. Decision snapshot vs lockstep required before M3.

## 1. Vendor vs Active

| Path | Role | Built? | Notes |
|---|---|---|---|
| `src/0ad/network/*` | **Vendor reference** — full 0AD netcode (`NetClient.h:52 NCS_*`, `NetServer.h:41 CNetServerTurnManager`, `NetMessages.h:28 PS_PROTOCOL_MAGIC 0x5073013f`, `NetHost.h:88 CNetHost::Initialize`, `NetServerTurnManager.cpp:235`, `NetClientTurnManager.cpp:188`, `NetFileTransfer`, STUN/UPnP `NetServer.cpp:220`, `lobby/XmppClient`) | **No** | Kept for reference & future porting; requires SpiderMonkey, `lib`s, lobby — not linked |
| `src/network/*` | **Active RTS0** — `turn_manager.h/.cpp` (single `TurnManager` `TURN_DURATION 0.200f` lockstep + `compute_sim_hash` FNV-1a `turn_manager.cpp:26` + `save_replay/load_replay` magic `0x52545330`), `net_host.h/.cpp` (`NetHost::startHost/startClient/poll/sendCommand` no-op `net_host.cpp:9`), `net_types.h` (`NetRole None/Host/Client`, `NetConfig port=7777 maxPlayers=8`) | **Yes** | Implements `Simulation::tick` fixed `1/20` integration but no real transport |

```
src/
  0ad/network/  ──► vendored 0AD (NetClient/NCS_*, NetServer, TurnManagers, NetMessages 28 types, ENetHost, file transfer, STUN/UPnP)
  network/      ──► active RTS0  (TurnManager single stub, NetHost no-op, NetTypes NetCommand raw binary)
  simulation/   ──► Simulation cmd_* / order_* drives TurnManager tick
  presentation/ ──► will own host/join UI (M3)
```

## 2. SConstruct:28 Exclusion

`SConstruct:24-30`:
```python
for root, dirs, files in os.walk("src"):
    # 0ad vendor is reference only — not built (requires 0 A.D. lib stubs, SpiderMonkey, etc.)
    # Real game uses SoA rewrite in src/sim + src/core with cache-friendly arena/pool.
    # Vendor files under src/0ad are kept for reference & future porting.
    if "0ad" in root.split(os.sep):
        continue
    for f in files:
        if f.endswith(".cpp"):
            sources.append(os.path.join(root, f))
```
`src/0ad/*` is **excluded from `sources`** → not compiled/linked into `bin/librtso.*`. Removing the `if "0ad"` guard would break build (missing `simulation2/system/TurnManager.h`, `ps/CStr`, `lib/types.h`, JS). To port a 0AD file, copy/adapt it into `src/network/` (or `src/core/`) under RTS0 types.

## 3. Current Stub Detail (`src/network/*`)

**`turn_manager.h:28`**
```cpp
struct NetCommand { uint32_t client_id; uint8_t cmd_type; vector<uint32_t> entity_ids; float target_x,z; uint32_t target_id; uint8_t param; };
struct TurnFrame { uint32_t turn_index; uint32_t sim_state_hash; vector<NetCommand> commands; };
class TurnManager { TURN_DURATION 0.200f; post_local_command(); tick(dt, sim); compute_sim_hash(); save/load_replay 0x52545330; };
```
`tick()` batches `pending_local_cmds_` → `TurnFrame` → `sim.cmd_move/stop/order_attack/...` (`turn_manager.cpp:72`), stores `sim_state_hash`.

**`net_host.h/.cpp:9`**
```cpp
class NetHost { NetConfig cfg; NetRole role; startHost(cfg)->true; startClient(host,port)->true; poll(Arena&)->nop; sendCommand(peer,data)->nop; onCommand(cb)->nop; stop()->None; };
```

**`net_types.h:6`**
```cpp
enum class NetRole { None, Host, Client };
struct NetConfig { uint16_t port=7777; uint8_t maxPlayers=8; };
struct NetPeer { uint32_t id; string address; uint16_t port; bool connected; };
```

Gaps vs 0AD counted in `docs/MULTIPLAYER.md:## 2026-08-29 Gap Detail` (7 rows): ENetHost init, NMT_* 28 types, hash quick/full, rejoin/fileTransfer, FSM `NCS_*`, lobby GUID `PlayerAssignmentMap`, NAT STUN/UPnP.

## 4. Future Split — `NetClientTurnManager` / `NetServerTurnManager`

> Target mirrors `src/0ad/network/NetClientTurnManager.h:37` (`CNetClientTurnManager : CTurnManager`) / `NetServerTurnManager.h:41` (split from single `TurnManager` `turn_manager.cpp:183`).

**`NetClientTurnManager` (`src/network/net_client_turn_manager.h/.cpp` — future)**
- Owns `CSimulation2& simulation`, `CNetClient& client`, `int clientId`, `IReplayLogger& replay`
- `OnSimulationMessage(CSimulationMessage*)`, `PostCommand(JS::HandleValue)`, `NotifyFinishedOwnCommands(turn)` → sends `NMT_END_COMMAND_BATCH{turn, turnLength}` + `NMT_SYNC_CHECK{turn, hash}`
- `NotifyFinishedUpdate(turn)` → hash `ComputeStateHash` (quick/full) comparison → `OnSyncError`
- `COMMAND_DELAY_MP` pipelining: executes `turn - COMMAND_DELAY_MP` only after server’s `EndCommandBatch`

**`NetServerTurnManager` (`src/network/net_server_turn_manager.h/.cpp` — future)**
- Owns `CNetServerWorker& server`, `m_ReadyTurn(COMMAND_DELAY_MP-1)`, `m_TurnLength(DEFAULT_TURN_LENGTH)`, `m_ClientsData: map<client,{readyTurn, simulatedTurn, isObserver, isOOS}>`, `m_ClientStateHashes: map<turn, map<client,hash>>`
- `NotifyFinishedClientCommands(session, turn)`, `NotifyFinishedClientUpdate(session, turn, hash)` → `CheckClientsReady()` → broadcasts `NMT_END_COMMAND_BATCH` when all clients `readyTurn >= m_ReadyTurn+1`
- `InitialiseClient(client, turn, observer)`, `UninitialiseClient(cl)`, `SetTurnLength(msecs)`, `GetReadyTurn()`, `GetSavedTurnLength(turn)`

Migration from single `TurnManager`:
```
before: TurnManager::tick() local + hash + push TurnFrame
after:  NetClientTurnManager (client thread) + NetServerTurnManager (server thread, CNetServerWorker)
        shared: NetCommand serialization -> CNetMessage (NMT_SIMULATION_COMMAND) + TURN_DURATION/COMMAND_DELAY_MP
```

## 5. ENet Linkage Plan

1. **Dependency:** add `enet` (`apt: libenet-dev` or `thirdparty/enet` git). `SConstruct` → `env.Append(LIBS=["enet"])`, `CPPPATH` for `lib/external_libraries/enet.h` vs system `<enet/enet.h>`.
2. **Host:** implement `src/0ad/network/NetHost.cpp:99 CreatePacket(CNetMessage)` + `CNetHost::Initialize/Deinitialize` (`enet_initialize`, `atexit(enet_deinitialize)`).
3. **NetHost:** replace `net_host.cpp:9` no-op with `ENetHost* m_Host; std::vector<ENetPeer*> m_Peers; enet_host_create(addr, peerCount=8, channelLimit=2, ...)`, `enet_host_service` loop in `poll()` (mirrors `NetServer.cpp:437 RunStep` / `NetSession.h:82 RunNetLoop`).
4. **Messages:** port or re-derive `NetMessages.h:28` (`PS_PROTOCOL_MAGIC 0x5073013f`, `PS_PROTOCOL_MAGIC_RESPONSE 0x50630121`, `PS_PROTOCOL_VERSION 0x01010019`) + `NetMessage` / `NMTCreator.h` serialization → `CNetMessage` `CreatePacket` / `CreateMessage`.
5. **Turn pipelining:** implement `NMT_END_COMMAND_BATCH` (`NetMessages.h:245 m_Turn u32 + m_TurnLength u16`) and `NMT_SYNC_CHECK`/`NMT_SYNC_ERROR` hash exchange; wire `ComputeStateHash` quick/full (currently single FNV-1a `turn_manager.cpp:26`).
6. **FSM:** add `NCS_*` FSM (`NetClient.h:52`) or simplified `NetRole` + `PlayerAssignmentMap` (`NetHost.h:38 GUID → PlayerAssignment`) + handshake `NMT_SERVER_HANDSHAKE`/`CLIENT_HANDSHAKE`/`AUTHENTICATE` (`NetMessages.h:106`).
7. **NAT:** optional `SetupUPnP` (`NetServer.cpp:220` `CONFIG2_MINIUPNPC`) + `TryToConnectWithSTUN` (`NetClient.h:147` / `XmppClient.cpp:887`); else require port-forward (doc `Transport` fallback WebSocket relay later).
8. **Testing:** `scons platform=linux target=template_debug && scons platform=linux target=template_release` — verify `src/0ad` still excluded; then `ENetHost` linkage test below.

## 6. Testing — `scons platform=linux` + 2 Clients

**Build:**
```bash
scons platform=linux target=template_debug -j4
# or
scons platform=linux target=template_release -j4
# output: bin/librtso.linux.template_debug.x86_64.so (or .so per suffix)
```

**Manual 2-client smoke (after ENet is linked, before split):**
```bash
# terminal 1 — host
./bin/RTS0 --host --port 7777 --map acropolis_bay
# terminal 2 — client
./bin/RTS0 --join 127.0.0.1 --port 7777
# expected after gap closure: handshake 0x5073013f -> NCS_PREGAME -> NCS_LOADING -> NCS_INGAME,
# TurnManager advances 5 turns/sec, NMT_END_COMMAND_BATCH every turn, no OOS (hashes match).
```

**Current stub test (pre-ENet):**
```bash
scons platform=linux -j4 && ./bin/RTS0 --test-turnmanager
# or unit: ctest / godot --headless --test sim
# verifies: TURN_DURATION 0.200f, save_replay 0x52545330 round-trip, FNV-1a hash stable, no ENet needed
```

**OOS regression (post-split):**
```bash
# force OOS: mutate sim on client 2, expect NMT_SYNC_ERROR(m_Turn, m_HashExpected, m_PlayerNames)
# server log: NotifyFinishedClientUpdate hash mismatch -> m_HasSyncError=true, m_ClientsData[cl].isOOS=true
```

## 7. File Map

```
src/network/
  README.md                ← this file
  turn_manager.h/.cpp      ← single lockstep stub (future: split into net_client_turn_manager.* / net_server_turn_manager.*)
  net_host.h/.cpp          ← NetHost no-op (future: ENetHost* + poll loop)
  net_types.h              ← NetRole/NetConfig/NetPeer + future NetCommand→CNetMessage

src/0ad/network/           ← vendor reference (not built, SConstruct:28 exclusion)
  NetMessages.h            ← PS_PROTOCOL_MAGIC 0x5073013f, NMT_* 28, EndCommandBatch/SyncCheck/SyncError
  NetHost.h                ← PlayerAssignment GUID map
  NetClient.h              ← NCS_UNCONNECTED..INGAME FSM
  NetServer.h/.cpp         ← CNetServerWorker + SetupUPnP
  NetServerTurnManager.*   ← 235 LOC, m_ReadyTurn COMMAND_DELAY_MP-1
  NetClientTurnManager.*   ← 188 LOC, : CTurnManager
  NetFileTransfer.*        ← FILE_TRANSFER_* chunking
  lobby/XmppClient.cpp     ← TryToConnectWithSTUN
```

## References

- `SConstruct:28` exclusion
- `src/0ad/network/NetMessages.h:28` `0x5073013f`, `src/0ad/network/NetServerTurnManager.cpp:43` `COMMAND_DELAY_MP`, `src/0ad/network/NetClient.h:52` `NCS_*`, `src/0ad/network/NetHost.h:38` `PlayerAssignment`
- `src/network/turn_manager.h:30` `TURN_DURATION`, `src/network/net_host.cpp:9` stub, `src/network/net_types.h:6` `NetRole`
- `docs/MULTIPLAYER.md:2026-08-29 Gap Detail` + `## Decision Matrix`, `docs/AI_PETRA.md` G10, `src/simulation/simulation.h:393` Petra comment
