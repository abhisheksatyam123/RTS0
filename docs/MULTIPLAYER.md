> **2026-08-29 Audit** TurnManager is single lockstep stub (turn_manager.cpp:183) vs 0AD split NetClientTurnManager:188/NetServerTurnManager:235. See docs/REMAINING_GAPS.md G8-G9. Decision required: snapshot (doc) vs lockstep (code).

# Multiplayer & Cross-Platform Plan

Targets: **Windows, Linux, Android, iOS** — online multiplayer.

## Platform matrix

| Platform | Renderer | Notes |
|---|---|---|
| Windows | Forward+ | Full support |
| Linux | Forward+ | Full support (+ headless dedicated-server export) |
| Android | Mobile | .NET export uses **AOT**; expect ~30 MB size increase; test early |
| iOS | Mobile | .NET export support is newer/experimental — **verify against your pinned Godot version before locking C# in** |

Rules of thumb:
- Pin one Godot version for the whole team; test Android builds weekly from day one.
- Avoid Forward+-only features (SDFGI, volumetric fog) — Mobile renderer must look acceptable.
- No right-click assumptions anywhere in UI/input code.

## Netcode model

### Chosen: Host-authoritative (client-server)
One peer hosts (or a dedicated Linux server later).

```
Client A/B/C          Host (= player 0 or dedicated)
    |  commands (small, reliable)   ^
    |------------------------------>|  simulate world at fixed tick
    |        snapshots (unreliable) |  broadcast compressed state
    |<------------------------------|
```

Why not deterministic lockstep (the classic AoE approach):
- Cross-platform float determinism (x86 vs ARM) forces fixed-point math for the entire simulation.
- Lockstep punishes mobile network jitter (everyone waits for slowest peer).
- Our scope (co-op/wave defense, hundreds of units — not 1000s) fits snapshots fine.

If we ever add ranked 1v1 PvP, revisit lockstep then — design the sim boundary below so that door stays open.

> **2026-08-29 Audit Note — Model Conflict:** This document describes **snapshot / host-authoritative** broadcast, but `src/network/turn_manager.h:30` and `turn_manager.cpp:183` implement a **deterministic lockstep** `TURN_DURATION = 0.200f` (5 turns/sec) matching 0AD `COMMAND_DELAY_MP` / `DEFAULT_TURN_LENGTH`. See `## 2026-08-29 Gap Detail (G8-G9)` and `## Decision Matrix` below. **Decision required before M3:** keep snapshot (rewrite TurnManager) or commit to lockstep (rewrite this doc + add fixed-point determinism).

## Architecture rules (start NOW, cheap to do today, painful to retrofit)

1. **Simulation / presentation split**
   - `core/` — pure game logic: unit state, economy, combat. No `Node3D`, no rendering, deterministic-friendly (fixed timestep, integer or grid coords where possible).
   - `presentation/` — visuals, audio, particles, camera. Reads sim state, never mutates it.
2. **Everything a player does becomes a serializable command**
   - `MoveCommand(unitIds[], Vector3 target)`, `BuildCommand(type, cell)`, etc.
   - Local input -> command -> (send to host) -> host validates & applies. Replay/anticheat/debug come almost free.
3. **Fixed timestep simulation** (e.g., 20 Hz), render decoupled with interpolation.
4. **IDs, not node references**, inside the sim (units, buildings, resources). Maps id -> entity on the presentation side.
5. **Input abstraction**: gameplay code consumes intents (`Select`, `OrderMove`), never raw mouse/touch events. Desktop and touch schemes both feed the same intents.

## Transport
- LAN/desktop: `ENetMultiplayerPeer` (UDP).
- Mobile/carrier NAT: same ENet usually OK with host having open port; fallback WebSocket relay later if needed.
- PC storefronts: GodotSteam for lobbies/overlay.
- Cross-play between stores: your own session service (even a tiny room-list HTTP API) keeps it store-agnostic.

## Mobile UX adaptations (plan early)
- Tap = select single; drag = box select; second-finger or virtual cursor for orders while dragging.
- Pinch zoom + one-finger drag pan (when nothing selected) or edge zones disabled.
- Build/train actions via bottom bar buttons, not hotkeys (hotkeys stay as desktop shortcuts).
- Target: playable one-handed portrait? No — landscape only, lock orientation.

## 2026-08-29 Gap Detail (G8-G9)

> G8 = Multiplayer Transport / TurnManager gaps. G9 = Petra AI parity. This table audits `src/network/*` vs vendored `src/0ad/network/*`. See `docs/REMAINING_GAPS.md` G8-G9 for tracking.

| Area | 0AD Reference | RTS0 Current (Stub) | Gap / File |
|---|---|---|---|
| **Transport / ENetHost Init** | `src/0ad/network/NetHost.h:88` `CNetHost::Initialize()` static + `NetServer.cpp:190` `enet_host_create(addr, peerCount, channelLimit)` + `NetClient.h:134` `SetupConnection(ENetHost*)` + `SetupUPnP` thread `NetServer.cpp:220` | `src/network/net_host.cpp:9` `startHost/startClient/poll/sendCommand` all no-op; `role=Host|Client` only, ignores `Arena&` | No ENet linkage, no `ENetHost* m_Host` / `ENetPeer*`, no `enet_host_service` loop |
| **Messages / Command Batch** | `src/0ad/network/NetMessages.h:78-84` `NMT_END_COMMAND_BATCH` (`m_Turn,u32` + `m_TurnLength,u16`) + `NMT_SIMULATION_COMMAND`, `NMT_SYNC_CHECK`/`NMT_SYNC_ERROR` (`m_Hash:CStr`), `NMT_CHAT/READY/GAME_SETUP/PLAYER_ASSIGNMENT/FILE_TRANSFER_*` etc — 28 message types `NetMessages.h:41-85` | `src/network/turn_manager.h:12` `NetCommand{client_id, cmd_type:u8, entity_ids[], target_x/z, target_id, param}` raw binary serialization `turn_manager.cpp:104` `magic 0x52545330 "RTS0"` | No `NMT_*` enum, no `CNetMessage` serialization, no per-turn batching, no chat/lobby handshake |
| **Hash / OOS Detection** | `src/0ad/network/NetServerTurnManager.h:95` `m_ClientStateHashes: map<turn, map<client, hash>>` + `NetClientTurnManager.cpp:52` `COMMAND_DELAY_MP` hash compare via `CStr m_Hash` (`NMT_SYNC_CHECK:250` / `NMT_SYNC_ERROR:255`) — quick hash + full hash `ComputeStateHash` | `src/network/turn_manager.cpp:26` `compute_sim_hash()` single FNV-1a 32b (`2166136261u * 16777619u`) over `pos_x/z, hp, action_state` + 4 resources `sim.food()/wood()/gold()/stone()` — no per-client map, no quick/full split | No divergent-hash OOS reporting, no `NMT_SYNC_ERROR` player-name list, single global hash stored `TurnFrame.sim_state_hash` |
| **Rejoin / FileTransfer** | `src/0ad/network/NetMessages.h:60-63` `NMT_FILE_TRANSFER_REQUEST/RESPONSE/DATA/ACK` + `NMT_JOIN_SYNC_START:65`, `NMT_REJOINED:67`, `NMT_LOADED_GAME:75` + `NetFileTransfer.h:41` chunked reliable transfer; `NetServer.cpp:1253` `NotifyFinishedClientUpdate` re-sync | `src/network/turn_manager.cpp:136` `load_replay(path)` local file only; `save_replay` / `load_replay` binary via `ifstream` — no network rejoin, no file transfer | Joining mid-game, re-sync, and saved-game transfer missing |
| **FSM / Session State** | `src/0ad/network/NetClient.h:52-59` `NCS_UNCONNECTED, NCS_CONNECT, NCS_HANDSHAKE, NCS_AUTHENTICATE, NCS_PREGAME, NCS_LOADING, NCS_JOIN_SYNCING, NCS_INGAME` + `NetServer.h:50` `NetServerState` + `NetServer.h:75` `NetServerSessionState` + `NetSession.h:158` `CNetServerSession : CFsm<>` threaded | `src/network/net_types.h:6` `enum class NetRole { None, Host, Client }` + `net_host.h:7` `NetRole role` only — no `CFsm`, no `boost::lockfree::queue<ENetEvent>` | No handshake/auth FSM, no `HandleMessageReceive`, no `RunNetLoop` thread |
| **Lobby / PlayerAssignments** | `src/0ad/network/NetHost.h:38` `struct PlayerAssignment {m_Enabled,m_Name:CStrW,m_PlayerID:i8,m_Status:u8}` + `NetHost.h:57` `PlayerAssignmentMap map<GUID, Assignment>` + `NetClient.h:363` `m_PlayerAssignments` + `NetMessages.h:161` `NMT_PLAYER_ASSIGNMENT` GUID routing, `m_GUID` per peer, `ARC_OK/REJOINING` | `src/network/net_types.h:7` `struct NetConfig {port=7777, maxPlayers=8}` + `struct NetPeer{id,address,port,connected}` — no GUID, no `PlayerAssignmentMap`, `maxPlayers=8` hard-coded | No lobby GUID, no `NMT_ASSIGN_PLAYER`/`NMT_PLAYER_ASSIGNMENT`, no observer slot, no `CStr m_GUID` |
| **NAT / STUN / UPnP** | `src/0ad/network/NetServer.cpp:220` `SetupUPnP(port)` via `miniupnpc` (`CONFIG2_MINIUPNPC`) + `NetClient.h:147` `TryToConnectWithSTUN(hostJID, localNetwork)` + `XmppClient.cpp:887` `g_NetClient->TryToConnectWithSTUN` + `NDR_STUN_PORT_FAILED:82` / `NDR_STUN_ENDPOINT_FAILED:83` | `src/network/net_host.cpp:9` none; `net_types.h` no STUN/UPnP fields | NAT punch-through absent — LAN + port-forward only; mobile/carrier NAT fallback WebSocket relay not implemented |

Additional 0AD anchors: `NetServerTurnManager.cpp:43` `m_ReadyTurn(COMMAND_DELAY_MP-1)`, `NetServerTurnManager.h:99` `m_ReadyTurn`, `NetClientTurnManager.h:37` `CNetClientTurnManager : CTurnManager` (`turn_manager.cpp:183` single stub vs split).

## Decision Matrix

> Must decide **before M3 (host/join UI)**. Snapshot is what `## Netcode model` (host-authoritative) describes; lockstep is what `src/network/turn_manager.*` currently implements (200ms lockstep, 5 turns/sec, `TURN_DURATION=0.200f`).

| Model | Pros | Cons | 0AD Reference & RTS0 Impact |
|---|---|---|---|
| **Snapshot / Host-authoritative** (doc model) | Cross-platform float-safe — no fixed-point rewrite needed (x86 vs ARM determinism avoided). Tolerant to mobile jitter — clients render latest snapshot interpolated, not blocked by slowest peer. Simpler transport: snapshots `unreliable` + commands `reliable` (doc `## Netcode model`). Host validates & applies `GameSim.Apply`. | Bandwidth heavy: full/delta state broadcast each tick; needs compression. Anti-cheat weaker (client trusts host snapshots). Divergence harder to detect without hashes. Requires implementing `NetHost::poll` snapshot send, client interpolation, compression. | Doc `## Netcode model:28-35` sized for co-op/wave defense (hundreds, not 1000s). If chosen, **rewrite** `src/network/turn_manager.h:28` — replace lockstep `TURN_DURATION`/`TurnFrame` with `SnapshotManager` (host tick 20 Hz, snapshot delta, client interp). Keep `NetCommand` but send host->all via `ENetHost` unreliable channel. |
| **Deterministic Lockstep** (code model) | Minimal bandwidth: only `NetCommand` batches per turn ( `turn_manager.cpp:67` `frame.commands = move(pending)`). Strong OOS detection via hashes (`compute_sim_hash` → `NMT_SYNC_CHECK:80` / `NMT_SYNC_ERROR:81`). Proven for RTS: 0AD `src/0ad/network/NetServerTurnManager.cpp:47` “Turns [1..COMMAND_DELAY-1] are special: all clients run without waiting on server”. Replay free (`save_replay` magic `0x52545330`). | Requires **full determinism**: fixed-point math or soft-float, deterministic `JobSystem` tick, no `float` drift — painful retrofit per doc warning. Lockstep punishes jitter: everyone waits `COMMAND_DELAY_MP` turns (`NetServerTurnManager.cpp:43` `m_ReadyTurn(COMMAND_DELAY_MP-1)=1`, `DEFAULT_TURN_LENGTH` `* COMMAND_DELAY_MP/2 = NETWORK_BAD_PING:98`). Needs split turn managers. | 0AD handshake: `NetMessages.h:28` magic `0x5073013f` (`'P','s',0x01,'?'`) + `0x50630121` response, `PS_PROTOCOL_VERSION 0x01010019`, `DEFAULT_TURN_LENGTH` + `COMMAND_DELAY_MP` (=2 typical, 200ms turn → 400ms command delay). RTS0 currently hard-codes `TURN_DURATION 0.200f` (`turn_manager.h:30`) but single `TurnManager` (`turn_manager.cpp:183` lines) vs 0AD split `NetClientTurnManager:188 LOC` / `NetServerTurnManager:235 LOC`. If chosen, **rewrite doc** `## Netcode model`, add `NetClientTurnManager`/`NetServerTurnManager` split, implement `ENetHost` + `COMMAND_DELAY_MP` pipelining, `NMT_END_COMMAND_BATCH` (`NetMessages.h:245`), and fixed-timestep determinism. |

**Recommendation placeholder (team decision required):** For RTS0 scope (co-op/wave, hundreds of units, mobile jitter-sensitive) the doc’s snapshot model is lower risk. For ranked 1v1 PvP or large battles the lockstep code model is correct but requires determinism investment. Either way, align `docs/MULTIPLAYER.md` ↔ `src/network/turn_manager.*` before M3.

## Milestones insertion (Annotated 2026-08-29)

- M1 ✅ **done** (verified): command pattern + core/presentation split exist (`core/GameSim.cs` / `core/Commands.cs` in doc; actual `src/simulation/simulation.h` `cmd_*`/`order_*` + `src/network/turn_manager.h:12` `NetCommand` — all input routes through serializable commands applied via `Simulation::tick` / `TurnManager::tick`). Parallelism inside tick is deterministic-safe (two-phase force pass), so does NOT block future lockstep.
- M2 **in progress / netcode-ready gap**: local-only game fully driven through commands. `Simulation::tick` fixed `FIXED_STEP 1/20` exists but `TurnManager::tick` (`turn_manager.cpp:52`) is single local lockstep stub — no network batching yet. Needs `NMT_SIMULATION_COMMAND` wire-up.
- M3 **host/join UI stub — NOT DONE**: `src/network/net_host.h:6` `NetHost{startHost,startClient,poll,sendCommand,onCommand}` all no-op `net_host.cpp:9` (ignores `Arena&`), `NetRole None/Host/Client` only vs `NCS_*` FSM. ENet not linked (see `## 2026-08-29 Gap Detail`). UI not wired. Decision snapshot vs lockstep must be resolved before implementing.
- M4 **not started**: mobile touch scheme + Android device testing. Blocked on M3 decision (snapshot more tolerant to carrier NAT jitter; lockstep needs `COMMAND_DELAY_MP` tuning + STUN/UPnP).
- M5 **not started / iOS verification gate** -> decide C# stays or hot paths move. (RTS0 is C++20 GDExtension — C# note is doc legacy; verify against pinned Godot version anyway.)

## 2026-08-29 G8/G9 Scaffold (Phase4) — minimal lift 5%→30% without ENet

> **Parity note:** TurnManager now hashes **all SoA fields** (`pos_x/z, hp, unit_type, team, action_state, carry, building hp/progress/team/type, resources, techMask, fow_grid_, projectiles, relics, territory_grid_, market prices`) via FNV-1a (`turn_manager.cpp:26 compute_sim_hash // Phase4 HashSerializer parity`, `v2` replay). `save_replay` writes `turn_index` monotonic + `sim_state_hash` per frame; `load_replay` verifies ordering and hash (accepts `v1` legacy). `NetHost` now exposes `NetMessageType : uint8_t { NMT_SIMULATION_COMMAND=29 ... NMT_SYNC_CHECK/ERROR }` + `struct NetMessage {type, turn, payload}` header-only, plus `poll()` pseudocode `enet_host_service` / `ENET_EVENT_TYPE_CONNECT/DISCONNECT/RECEIVE` dispatching `onCommand`. `NetTypes` adds `Observer` role, `rtt_ms`, `snapshotRate 20Hz`. No `libenet` linkage required yet — `g++ -fsyntax-only` passes. Next: wire `ENetHost* m_Host` in `poll()` and coalesce `NMT_END_COMMAND_BATCH` in `NetServerTurnManager`.


## Playtest harness (2026-10-05)

Headless two-client lockstep OOS hunt without Godot/SCons (`docs/PLAN.md` P1):

```bash
python3 tools/playtest_lockstep.py --ticks 2000        # compiles tools/test_netcode.cpp, 2 seeds
python3 tools/playtest_lockstep.py --help              # --seeds/--shadow/--through/--timeout
# or drive the prebuilt binary directly:
~/tmp/playtest/netcode_playtest --playtest <turns> <seed> [--shadow] [--through] [--verbose N]
```

- Exit 0 = all seeds in sync, 1 = first OOS turn dumped with context, 2 = build failure.
- `--shadow` adds a third command-free peer; `--through` keeps hashing after match end (defeats frozen-sim false negatives). `tools/test_netcode.cpp` `--playtest` prints per-turn FNV-1a of all peers.
- Link-probe note: `src/core/diagnostics/*` excluded (pulls godot-cpp, cannot link engine-less).

**Result 2026-10-05:** seeds `4242`, `777001`, `20261005` × 2000 turns, `--shadow --through` → **3/3 PASS**, peers A=B=C every turn (e.g. seed 4242 `0x834C102F`, rng `3204712309` on all three). Zero OOS. Complements `tools/test_replay.cpp` (save_replay/load_replay round-trip + negative control, RC=0).

## Two-client playtest automation (2026-10-05)

Real host/join session driver: `tools/playtest_two_clients.py` (no CLI trigger exists — handlers are GUI-bound at `main_node.cpp:547/551/560`, host `:3634`, join `:3765`, start `:3655`; `scenes/menu/lobby.gd` is a print-only stub). The script runs two real Godot clients on **Xvfb** displays and drives the actual UI with **xdotool** (verified click coords at 3840x1080: Lobby `(3024,46)`, Host `(41,419)`, Join `(145,419)`; readiness gate = `[mem]` sim heartbeat at ~30-75s; single-click policy to avoid peer churn).

```bash
python3 tools/playtest_two_clients.py --probe          # env check (lobby_server, Xvfb, xdotool) — RC 0
python3 tools/playtest_two_clients.py --run --minutes 5 # full session — artifacts in $HOME/tmp/playtest_two_clients/
```

- Exit 0 = session survived N minutes, no OOS/error/disconnect markers; 1 = OOS/error evidence dumped; 2 = environment failure.
- Markers verified YES: `host_listening`, `peer_accepted`, `client_connecting`, `client_connected`, `sync_exchange`. Teardown-time "disconnected" lines are excluded from findings.
- Honest limitations: `game_start` log marker may not match string-wise (NMT_GAME_START handling in `apply_net_game_start` — verify); fps=1-5 under Xvfb (software GL) does not affect lockstep; pause-menu "Lobby" route pauses the sim (`main_node.cpp:1666` `paused_via_menu`) and must NOT be used — the script clicks the top-bar Lobby button (visible only at >=3840 window width).

**First live OOS found 2026-10-05 (turn 1):** `local_hash=2731923309 vs peer_hash=3034033795` — immediate divergence, not drift; contrast `playtest_lockstep.py` (identical-command harness) which passes.

**ROOT-CAUSE + FIX 2026-10-06:** lockstep turns and sync-hash exchange started at Host/Join click, BEFORE Start — the peers hashed their *independent boot worlds* from turn 1 (and `reset()` at asymmetric wall times risked turn-counter offsets). Fix: `net_game_started_` gate (`main_node.h`) — turns/sync stay paused until NMT_GAME_START is applied on each peer (host at `_on_lobby_start_pressed`, joiner at `apply_net_game_start`); engine prints `[RTS0][GameStart] HOST broadcast setup` / `JOINER consumed` with map/seed/civs so runs are verifiable. The harness was made STRICT: it now sweeps for the Start button until the host broadcast line appears (Start measured at x=276,419 @3840x1080 — old pitch-estimate 353 missed) and fails `EXIT_NO_SESSION` if either peer's gate never opened (prevents vacuous passes). **Verified 2026-10-06: `--run --minutes 1` -> start_host YES, start_joiner YES, OOS 0, errors 0, EXIT 0.**

---

**Wave 1+2 parity-bookkeeping refresh 2026-10-06** (doc bookkeeping only; no `src/` changes). Landed-network items now recorded as shipped, with grep evidence:

- **Start gate** — `src/presentation/main_node.h:185` `bool net_game_started_ = false;`; host broadcast `src/presentation/main_node.cpp:3743-3745` (`mark_game_started` + `note_game_start_sent` + `sendGameStart(format_game_start_full(setup))`); joiner consume `main_node.cpp:3803-3810` (`apply_net_game_start` → `take_pending_game_start_full` → `note_game_start_applied`).
- **`NMT_GAME_START` resend** — `src/network/net_host.cpp:466 sendGameStartTo(peer_id, attributes)` resends the SAME authoritative setup to one late-joining peer; gated on `game_start_sent_` (`net_host.h:81-82`, `:175-177`, reset at `net_host.cpp:103/:157/:891`). Answers the "rejoin / join-sync" row that previously read *missing* above.
- **Join-sync (turn-0 state resync)** — `NMT_JOIN_SYNC_STATE = 32` / `NMT_JOIN_SYNC_CHECK = 33` (`src/network/net_types.h:68`/`:71`), handshake documented `net_types.h:394-445`; joiner side `net_host.cpp:245 request_join_sync()`; host side `TurnManager::answer_join_sync` (`turn_manager.h:123`, `.h:153`).
- **Fleet upkeep command** — `NET_CMD_MAINTAIN_FLEET = 24` (`net_types.h:134`); generator `turn_manager.cpp:481-551` (emits at `:544`), dispatch `:559 maintain_fleet_upkeep` called from `tick()` at `:861`; sim hook `Simulation::cmd_maintain_fleet` (`simulation.h:195`, impl `simulation.cpp:835`).
- **Diplomacy via TurnManager** — `post_diplomacy` (`turn_manager.h:98`) queues stance through the same lockstep command stream; `apply_diplomacy()` (`:326-328`) is the single sim write point at the turn boundary (mirrors the ROOT-CAUSE/FIX note above: no pre-Start writes).

G8 table value stays **98%** and G9 stays **96%** — the remaining gate for both is a real 2-Godot-client session / NAT, not missing protocol code. Overall `check_parity.py` parity **90% → 92%**.
