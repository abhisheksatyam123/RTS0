# Multiplayer & Cross-Platform Plan

Targets: **Windows, Linux, Android, iOS** — online multiplayer. Remaining multiplayer work is tracked in [PLAN.md](PLAN.md) (P1 two-client gate, P5 ranked lockstep). This file describes what is built.

## Platform matrix

| Platform | Export preset (`export_presets.cfg`) | Notes |
|---|---|---|
| Windows | "Windows Desktop" | Native C++ GDExtension build |
| Linux | "Linux" | Native C++ GDExtension build |
| Android | "Android" | Same C++ GDExtension via godot-cpp; test device builds early |
| iOS | none yet | No export preset in the repo; export and runtime checks are open in PLAN.md |

Rules of thumb:
- Pin one Godot version for the whole team: 4.3, matching the godot-cpp 4.3 branch that `SConstruct` builds against. Test Android builds weekly.
- The build is C++20 (`SConstruct`, `-std=c++20`). There is no C#, .NET or AOT step.
- Avoid Forward+-only features (SDFGI, volumetric fog); the Mobile renderer must look acceptable.
- No right-click assumptions anywhere in UI/input code.

## Netcode model: deterministic lockstep (built)

Every peer runs the same simulation and exchanges only player commands. A hash of the simulation state after each turn detects divergence.

- **Turns:** 200 ms (`TurnManager::TURN_DURATION`, `src/network/turn_manager.h:27`) = 4 simulation ticks at 1/20 s (`TICKS_PER_TURN`, `turn_manager.h:28`; `Simulation::FIXED_STEP`, `src/simulation/simulation.h:79`).
- **Commands:** player actions become `NetCommand`s (`src/network/net_types.h`; `cmd_type` 1..23 is frozen). Each peer posts its own commands through `TurnManager::post_local_command` and receives the others'. The batch is applied on every peer at the turn boundary (`NMT_SIMULATION_COMMAND` + `NMT_END_COMMAND_BATCH`, `net_types.h:53-56`).
- **Desync detection:** after each turn, `TurnManager::compute_sim_hash` (each word avalanched with MurmurHash3's fmix32, over the simulation's structure-of-arrays state, `turn_manager.h:175`) is stored in `TurnFrame::sim_state_hash` and exchanged as `NMT_SYNC_CHECK`. A mismatch is reported as out-of-sync (`NMT_SYNC_ERROR`; `turn_manager.cpp:215-225`, `:915`).
- **Start gate:** ticks are held until `NMT_GAME_START` has been applied on that peer (`net_game_started_`, `src/presentation/main_node.h:168`; `TurnManager::net_game_started()`). Without this, peers hash independent boot worlds.
- **Late joiners:** the host resends the setup to one late peer (`NetHost::sendGameStartTo`, `src/network/net_host.cpp:466`) and checks turn-0 state with `NMT_JOIN_SYNC_STATE` / `NMT_JOIN_SYNC_CHECK` (`net_types.h:68`, `:71`).
- **Other commands on the same path:** diplomacy stances (cmd 22, run as the sender's seated player; the old `NMT_DIPLOMACY` carrier is retired and dropped on receipt) and fleet upkeep (`NET_CMD_MAINTAIN_FLEET = 24`, `net_types.h:134`) are lockstep commands, not separate messages.
- **Determinism:** the simulation uses `float`, and every build must compute the same bits. `SConstruct` turns FMA contraction off (`-ffp-contract=off`, `/fp:precise` on MSVC): clang fuses `a*b+c` on arm64 by default, which made Apple Silicon, iOS and Android builds desync from PC builds on turn 1. Never draw from the map or sim RNG twice in one expression: g++ and clang order the draws differently. Simulation code calls `det::sin`, `det::cos`, `det::atan2`, `det::acos`, `det::hypot`, `det::pow`, `det::log` and `det::exp` from `src/core/math/det_math.h`, never the C library, whose last bit differs between OSes; `tools/check_det_math.py` rejects a direct libm call and `tools/test_det_math.cpp` pins the result bits. `tools/test_cross_compiler.py` checks all of this on every CI run (g++ against `clang++ -mfma`, every random map and a 12-minute Petra match, and no libm maths symbol in the simulation objects). Builds that stop computing the same match raise `NET_SOFTWARE_VERSION`, so a host refuses older ones. Still open (REMAINING_GAPS G30): no run has yet paired two real devices of different platforms.
- **Fixed-point:** `src/core/math/fixed.h` (16.16) is a port of 0 A.D.'s fixed-point math. The simulation does not include it. It is the candidate for ranked 1v1 only (`docs/PLAN.md` P5).

## Architecture rules (start NOW, cheap to do today, painful to retrofit)

1. **Simulation / presentation split**
   - `src/simulation/` — pure game logic: units, economy, combat, navigation, Petra AI. No Godot types.
   - `src/presentation/` — Godot nodes (`RtsoMain`, world and minimap views, HUD panels). Reads sim state; changes it only through `Simulation` command methods (`cmd_*`, `order_*`) or lockstep posts.
   - `src/network/` — `TurnManager` (lockstep) and `NetHost` (transport). `src/core/` — job system, math, memory.
2. **Everything a player does becomes a serializable command**
   - `NetCommand` (`src/network/net_types.h`) carries unit ids, target, and type. In multiplayer, input goes through `TurnManager::post_local_command` and is applied at the turn boundary.
   - Replay, anti-cheat and debugging follow from this (`save_replay` / `load_replay`).
3. **Fixed timestep simulation**: sim ticks at 1/20 s; the lockstep turn is 200 ms (4 ticks). Rendering interpolates between positions (`src/presentation/world_view_node.cpp:1150-1158`).
4. **IDs, not node references**, inside the sim (units, buildings, resources). Presentation maps id to node.
5. **Input abstraction**: gameplay code consumes intents (`Select`, `OrderMove`), never raw mouse/touch events. Desktop and touch schemes both feed the same intents.

## Transport

- **Custom UDP (`NetHost`, `src/network/net_host.{h,cpp}`):** raw BSD sockets (`socket(AF_INET, SOCK_DGRAM)`, `sendto` / `recvfrom`). Packets carry a `NetWireHeader` with the 0 A.D. protocol magic (`PS_PROTOCOL_MAGIC`, `net_types.h`), flags for connect / ack / reliable / heartbeat, and ACK-based retry for reliable packets (`PendingReliablePacket`). Default port 20595 (`PS_DEFAULT_PORT`).
- **Not ENet.** No ENet dependency appears in `SConstruct` or `src/`. The vendored `src/0ad/network/` is reference only and is not built.
- **Lobby:** HTTP JSON room list on TCP 8088, served by `tools/lobby_server.py` (`--port`, default 8088). Client: `src/network/lobby_client.h`.
- **NAT:** STUN and hole punching through the lobby, and a host maps its UDP port on the router with UPnP (0.28 NetServer SetupUPnP; `network.upnp = false` turns it off). Without either, play over LAN or a forwarded UDP port.
- **Not implemented:** storefront lobbies and overlay, and a relay fallback for carrier NAT.

## Mobile UX adaptations (plan early)
- Tap = select single; drag = box select; second-finger or virtual cursor for orders while dragging.
- Pinch zoom + one-finger drag pan (when nothing selected) or edge zones disabled.
- Build/train actions via bottom bar buttons, not hotkeys (hotkeys stay as desktop shortcuts).
- Landscape only; lock orientation.

## Playtest harness (2026-10-05)

Headless two-client lockstep OOS hunt without Godot/SCons (`docs/PLAN.md` P1):

```bash
python3 tools/playtest_lockstep.py --ticks 2000        # compiles tools/test_netcode.cpp, 2 seeds
python3 tools/playtest_lockstep.py --help              # --seeds/--shadow/--through/--timeout
# or drive the prebuilt binary directly:
~/tmp/playtest/netcode_playtest --playtest <turns> <seed> [--shadow] [--through] [--verbose N]
```

- Exit 0 = all seeds in sync, 1 = first OOS turn dumped with context, 2 = build failure.
- `--shadow` adds a third command-free peer; `--through` keeps hashing after match end (defeats frozen-sim false negatives). `tools/test_netcode.cpp` `--playtest` prints per-turn state hash of all peers.
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

### P1 Final Gate: 5-Minute Two-Client Session Drift Hunt (Verified 2026-10-06)

Full two-client end-to-end playtest executed via `tools/playtest_two_clients.py --run --minutes 5` against Godot 4.3 GDExtension (`43f1f45`):

- **Environment & Probe**:
  - Godot binary: `4.3.stable.official.77dcf97d8`
  - Helpers: `Xvfb` on displays `:98` & `:99`, `xdotool`
  - Ports: UDP/20595 (lockstep) free, TCP/8088 (lobby HTTP) free
  - Engine + GDExtension boot: Headless heartbeat OK (rc=0)
- **Session Execution**:
  - `start_host`: **YES** (`[RTS0][GameStart] HOST broadcast setup: map=random_continental seed=56334 slots=2 civ0=0 civ1=1 types=104/97 res=1`)
  - `start_joiner`: **YES** (`[RTS0][GameStart] JOINER consumed NMT_GAME_START: map=random_continental seed=56334 slots=2 civ0=0 civ1=1 types=104/97 res=1`)
  - Markers verified: `host_listening` YES, `peer_accepted` YES, `client_connecting` YES, `client_connected` YES, `game_start` YES, `start_host` YES, `start_joiner` YES
  - Session duration: **5.00 min** (300.0s hold under live lockstep sim)
  - Unit count progression: Synchronous 22 → 23 → 24 units across both host and joiner
  - Turns reached: ~1500 lockstep turns (at 200ms/turn over 300s)
  - **OOS count**: **0**
  - Error patterns: 0
  - Disconnect patterns: 0
  - **Exit code**: **0** (`VERDICT: two-client session survived 5.00 min with no OOS, desync, disconnect or error pattern.`)
- **Determinism Stream Cross-Check (Drift Suspects)**:
  - **Petra AI rand**: Confirmed deterministic. `Simulation::tick_petra_ai()` uses the simulation deterministic PRNG (`ScriptRng` / `sim_rng_state()`, `src/simulation/simulation.h:23`), completely avoiding host-dependent libc `rand()`. Headless lockstep (`tools/playtest_lockstep.py` ticks=400 & ticks=2000) with `petra_ai=on (difficulty=3, behavior=2)` confirmed 100% sync (final hashes match across peers A=B=C).
  - **Wall-drag spawn**: `BLD_WALL_SEGMENT` commands (`cmd_type 5`) route strictly through lockstep serialization (`TurnManager::post_local_command` / `post_remote_command`), sorted deterministically by `(client_id, cmd_type, target_id)` at the turn boundary before `Simulation::cmd_build()` and `Simulation::order_build_or_repair()`. Tested in multi-segment wall sweeps in `tools/test_netcode.cpp:230-265` without any desync.
